"""Fixed new-room TRAIN/CAL/RESERVE recipes; large arrays only in RAM.

Paired poses retain source world coordinates. No old mixtures/features are read.
The separate original PILOT and its sealed generator are left unchanged.
"""
from copy import deepcopy
from pathlib import Path
import json
import math
import sys
import numpy as np
from scipy.signal import fftconvolve,lfilter
from research.manual_room_simulation import load_config,validate,rir_arguments,digest
from research.candidate_rescue_synthesis import load_speech,rms,array_sha
from research.spatial_diffuse_background import SpatialBackground
from acoustic_camera.valid_source_detection import angular_distance_deg

ROOT=Path(__file__).resolve().parents[2]
CONFIG=ROOT/'config/simulation/meeting_room_research.json'
SPEECH_LOCK=ROOT/'results/candidate_speech_intake_dev_other_20261007/speech_assets_lock.json'


class MeetingRoomResearchDataset:
    def __init__(self, config_path=CONFIG):
        self.config_path=Path(config_path)
        self.config=json.loads(self.config_path.read_text(encoding='utf-8'))
        p=self.config
        assert p['schema_version']==1 and p['window_samples']==40960
        assert p['poses_per_world_layout']==2 and p['windows_per_pose']==16
        self.base=load_config(ROOT/p['manual_room_config'])
        assert self.base['config']['rir']['sample_rate_hz']==39062.5
        assert self.base['config']['room']['sound_speed_m_s']==343.
        self.fs=39062.5;self.N=40960
        rows=json.loads(SPEECH_LOCK.read_text(encoding='utf-8'))['speech_assets']
        pilot_speakers={'116','700'}
        cal_ids=sorted({r['speaker_id'] for r in rows if r['role']=='CAL'})
        self.asset_groups={'TRAIN':[r for r in rows if r['role']=='TRAIN' and r['speaker_id'] not in pilot_speakers],
            'CAL':[r for r in rows if r['speaker_id'] in cal_ids[:4]],
            'RESERVE':[r for r in rows if r['speaker_id'] in cal_ids[4:]]}
        for role,assets in self.asset_groups.items():
            assert assets and all(digest(Path(r['path']))==r['sha256'] for r in assets)
        ids={role:{r['speaker_id'] for r in assets} for role,assets in self.asset_groups.items()}
        assert all(not ids[a]&ids[b] for a,b in (('TRAIN','CAL'),('TRAIN','RESERVE'),('CAL','RESERVE')))
        self.speech={};self.activity_metadata={};self.states=[];self.layouts=[];self.plan=[]
        rng=np.random.default_rng(p['seed'])
        for role,groups in p['split_geometry_groups'].items():
            for group in range(groups):
                layout_id=f'{role.lower()}_world_{group:02d}'
                scale=p['room_dimension_scales'][(group+list(p['split_geometry_groups']).index(role))%len(p['room_dimension_scales'])]
                dims=np.asarray(self.base['room_size_m'])*scale
                rt60=p['target_rt60_s'][(group+list(p['split_geometry_groups']).index(role))%len(p['target_rt60_s'])]
                states,attempts=self._world_layout(layout_id,dims,rt60,rng)
                self.layouts.append(dict(layout_id=layout_id,role=role,geometry_attempts=attempts,
                    world_sources=states[0]['config']['sources'],state_indices=[len(self.states),len(self.states)+1]))
                for pose,state in enumerate(states):
                    state['research_role']=role
                    state_index=len(self.states);self.states.append(state)
                    assets=self.asset_groups[role]
                    for variant in range(4):
                        for count in range(4):
                            i=len(self.plan);seed=p['seed']+1009*(i+1)
                            machine='fan' if variant%2==0 else 'motor'
                            kinds=[] if count==0 else [machine] if count==1 else ['speech',machine] if count==2 else ['speech','speech',machine]
                            sources=[]
                            available=sorted({a['speaker_id'] for a in assets})
                            speakers=[available[(group*2+variant)%len(available)],available[(group*2+variant+1)%len(available)]]
                            for slot,kind in enumerate(kinds):
                                src=dict(slot=slot,kind=kind,seed=seed+slot*100003,
                                    gain_db=p['source_gain_patterns_db'][variant][slot])
                                if kind=='speech':
                                    subset=[a for a in assets if a['speaker_id']==speakers[slot]]
                                    asset=subset[(group+pose+variant)%len(subset)]
                                    src['asset_sha256']=asset['sha256'];src['asset_path']=asset['path']
                                    src['speaker_id_audit_only']=asset['speaker_id']
                                sources.append(src)
                            self.plan.append(dict(index=i,scene_id=f'{layout_id}_p{pose}_v{variant}_K{count}',
                                role=role,layout_id=layout_id,state_index=state_index,pose_index=pose,
                                source_recipes=sources,configured_emitting_points=count,variant=variant,
                                background_seed=seed+1000003,reference_seed=seed+7000001,
                                background_rms=10**(p['background_rms_dbfs'][variant]/20)))
        self.rirs=None;self.rir_state=None
        self.background=SpatialBackground(self.states[0]['microphone_positions_m'],
            diffuse_fraction=p['background']['diffuse_power_fraction'],ar1=p['background']['ar1_coefficient'],
            fft_size=p['background']['fft_size'])

    def _world_layout(self,name,dims,rt60,rng):
        p=self.config
        for attempt in range(1,p['maximum_geometry_attempts']+1):
            source_positions=np.column_stack([rng.uniform(.18,.82,(3,2))*dims[:2],rng.uniform(.7,1.8,3)])
            center=np.r_[rng.uniform(.12,.88,2)*dims[:2],rng.uniform(.9,1.4)]
            shifted=center+np.r_[rng.uniform(-.9,.9,2),rng.uniform(-.15,.15)]
            if np.linalg.norm(center[:2]-shifted[:2])<.4:continue
            states=[]
            for pose,location in enumerate((center,shifted)):
                config=deepcopy(self.base['config']);config['scene_id']=f'{name}_pose{pose}'
                config['room']['dimensions_m']=dims.tolist();config['room']['target_rt60_s']=rt60
                config['array_pose']['center_m']=location.tolist()
                delta=source_positions.mean(axis=0)-location
                config['array_pose']['yaw_deg']=float(np.rad2deg(np.arctan2(-delta[0],delta[1])))
                # 保留用户固定俯仰/滚转，偏航使视场朝向声源。
                config['sources']=[dict(id=f'S{j+1}',position_m=x.tolist()) for j,x in enumerate(source_positions)]
                config['rir']['duration_s']=max(p['rir']['minimum_duration_s'],p['rir']['duration_RT60_multiplier']*rt60)
                config['rir']['reflection_order']=p['rir']['reflection_order']
                try:state=validate(config)
                except ValueError:break
                src=state['sources']
                if not all(s['inside_current_search_FOV'] and p['minimum_source_range_m']<=s['distance_m']<=p['maximum_source_range_m'] for s in src):break
                if min(angular_distance_deg(a['azimuth_deg'],a['elevation_deg'],b['azimuth_deg'],b['elevation_deg'])
                       for j,a in enumerate(src) for b in src[j+1:])<p['minimum_angular_separation_deg']:break
                state['actual_background_model']='spherical_diffuse_plus_sensor'
                state['warnings']=[w for w in state['warnings'] if '独立AR' not in w]
                states.append(state)
            if len(states)==2:return states,attempt
        raise RuntimeError('bounded geometry construction failed: '+name)

    def prepare_audio(self,role):
        if role=='RESERVE':raise ValueError('RESERVE audio is sealed until final protocol approval')
        if role not in self.activity_metadata:
            audio,metadata=load_speech(self.asset_groups[role]);self.speech.update(audio)
            self.activity_metadata[role]=metadata
        return self.activity_metadata[role]

    def generate_state_rirs(self,index):
        if self.states[index]['research_role']=='RESERVE':
            raise ValueError('RESERVE RIRs are sealed until final protocol approval')
        runtime=ROOT/'.runtime/rir_generator_0_3_0'
        lock=json.loads((ROOT/'results/candidate_rir_preparation_20261007/protocol_lock.json').read_text(encoding='utf-8'))
        for info in lock['pinned_package'].values():assert digest(Path(info['path']))==info['sha256']
        sys.path.insert(0,str(runtime));import rir_generator as gen
        assert gen.__version__=='0.3.0'
        state=self.states[index]
        self.rirs=[gen.generate(**rir_arguments(state,s),mtype=gen.mtype.omnidirectional).astype(np.float32)
                   for s in state['sources']]
        assert all(h.shape==(state['sample_count'],16) and np.isfinite(h).all() for h in self.rirs)
        self.rir_state=index
        return self.rirs

    def sample(self,index):
        recipe=self.plan[index];role=recipe['role'];self.prepare_audio(role)
        state=self.states[recipe['state_index']]
        if self.rir_state!=recipe['state_index']:self.generate_state_rirs(recipe['state_index'])
        H=state['sample_count'];size=H+self.N;p=self.config
        background=self.background.generate(self.N,recipe['background_rms'],recipe['background_seed'])
        reference=self.background.generate(8192,recipe['background_rms'],recipe['reference_seed']).reshape(4,2048,16)
        pcm=background.astype(float);components=[];masks=[];details=[]
        for src in recipe['source_recipes']:
            slot=src['slot'];rng=np.random.default_rng(src['seed']);kind=src['kind'];extra={}
            if kind=='speech':
                audio,mask=self.speech[src['asset_sha256']]
                assert len(audio)>=size
                crop=int(rng.integers(0,len(audio)-size+1))
                dry=audio[crop:crop+size].astype(float)*p['emitter_digital_rms']/rms(audio)
                emitted=mask[crop:crop+size];extra=dict(original_asset_sha256=src['asset_sha256'],crop_start_sample=crop)
            else:
                axis=np.arange(size)/self.fs
                if kind=='fan':dry=lfilter([1.],[1.,-.91],rng.standard_normal(size))+.2*np.sin(2*np.pi*173*axis)
                else:dry=np.sin(2*np.pi*230*axis)+.35*np.sin(2*np.pi*460*axis)+.08*rng.standard_normal(size)
                dry=dry*p['emitter_digital_rms']/rms(dry);emitted=np.ones(size,bool)
            dry*=10**(src['gain_db']/20)
            component=fftconvolve(dry[:,None],self.rirs[slot],mode='full',axes=0)[H:H+self.N].astype(np.float32)
            pcm+=component;components.append(component)
            source=state['sources'][slot];delay=source['distance_m']*self.fs/343.
            ids=np.floor(H+np.arange(self.N)-delay+.5).astype(int)
            assert np.all((ids>=0)&(ids<size));active=emitted[ids];masks.append(active)
            details.append(dict(source_id=source['id'],source_slot=slot,direction=[source['azimuth_deg'],source['elevation_deg']],
                position_m=source['position_m'],distance_m=source['distance_m'],active_samples=int(active.sum()),
                activity_mask_sha256=array_sha(active),component_sha256=array_sha(component),component_rms=rms(component),
                component_to_background_db=20*math.log10(max(rms(component),1e-30)/rms(background)),
                emitted_gain_db=src['gain_db'],waveform_kind_audit_only=kind,**extra))
        pcm=pcm.astype(np.float32)
        assert np.isfinite(pcm).all() and np.max(np.abs(pcm))<1,'clipping: stop, no reroll or receiver renormalization'
        return dict(frames=pcm.reshape(20,2048,16),reference=reference,background=background,
            components=components,activity_masks=masks,record=dict(**recipe,source_details=details,
                pcm_sha256=array_sha(pcm),reference_sha256=array_sha(reference),pcm_peak_abs=float(np.max(np.abs(pcm))),
                pcm_saved=False,receiver_level_renormalized=False,actual_background_model=p['background']['model']))
