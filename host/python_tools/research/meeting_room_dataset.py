"""New configurable room, lazy RIR/PCM generation; no old scene/cache reads.

This bounded pilot reuses encoded dry speech assets, not old room mixtures.
Emitter gain is set before propagation. No source-specific receiver RMS gain.
"""
from copy import deepcopy
from pathlib import Path
import json
import math
import sys
import numpy as np
from scipy.signal import fftconvolve, lfilter
from research.manual_room_simulation import load_config, validate, digest, rir_arguments, background_from_config
from research.candidate_rescue_synthesis import load_speech, rms, array_sha

ROOT=Path(__file__).resolve().parents[2]
PILOT_CONFIG=ROOT/'config/simulation/meeting_room_pilot.json'
SPEECH_LOCK=ROOT/'results/candidate_speech_intake_dev_other_20261007/speech_assets_lock.json'


class MeetingRoomDataset:
    """Pose-major16-window development pilot; arrays stay in process memory."""
    def __init__(self, config_path=PILOT_CONFIG):
        self.pilot=json.loads(Path(config_path).read_text(encoding='utf-8'))
        p=self.pilot
        assert p['schema_version']==1 and p['window_samples']==40960
        assert 0<p['emitter_digital_rms']<1 and len(p['source_gain_db'])==3
        self.base=load_config(ROOT/p['room_config'])
        assert self.base['config']['rir']['sample_rate_hz']==39062.5, 'current model input requires39062.5Hz'
        assert self.base['config']['room']['sound_speed_m_s']==343., 'current physical/model geometry requires343m/s'
        assert len(self.base['sources'])==2, 'pilot uses two manually placed sources plus one configurable local position'
        self.N=40960;self.fs=39062.5;self.H=self.base['sample_count']
        room=np.array(self.base['room_size_m']);center=np.array(self.base['array_center_m'])
        rotation=np.array(self.base['rotation_matrix'])
        local=[rotation.T@(np.array(s['position_m'])-center) for s in self.base['sources']]
        local.append(np.asarray(p['third_source_local_position_m'],float))
        self.states=[]
        specs=[dict(id='manual_pose',center=center.tolist(),yaw=self.base['config']['array_pose']['yaw_deg'])]
        for pose in p['additional_array_poses']:
            specs.append(dict(id=pose['id'],center=[room[0]*pose['center_xy_fraction'][0],
                room[1]*pose['center_xy_fraction'][1],center[2]],yaw=pose['yaw_deg']))
        for spec in specs:
            config=deepcopy(self.base['config']);config['scene_id']='meeting_'+spec['id']
            config['array_pose']['center_m']=spec['center'];config['array_pose']['yaw_deg']=spec['yaw']
            # 每个小规模测试姿态都保留用户俯仰与滚转。
            config['sources']=[];pose_state=validate(config)
            r=np.array(pose_state['rotation_matrix']);c=np.array(spec['center'])
            config['sources']=[dict(id=f'S{j+1}',position_m=(c+r@offset).tolist()) for j,offset in enumerate(local)]
            state=validate(config)
            assert all(s['inside_current_search_FOV'] for s in state['sources']), 'pilot geometry outside current detector FOV'
            self.states.append(state)
        rows=json.loads(SPEECH_LOCK.read_text(encoding='utf-8'))['speech_assets']
        chosen=[]
        for row in rows:
            if row['role']=='TRAIN' and row['speaker_id'] not in {s['speaker_id'] for s in chosen}:
                assert digest(Path(row['path']))==row['sha256']
                chosen.append(row)
                if len(chosen)==2:break
        assert len(chosen)==2
        self.assets=chosen;self.speech=None;self.rirs=None;self.rir_pose=None
        self.plan=[]
        for pose_index,state in enumerate(self.states):
            machine='fan' if pose_index%2==0 else 'motor'
            for requested in range(4):
                kinds=[] if requested==0 else ([machine] if requested==1 else
                    ['speech',machine] if requested==2 else ['speech','speech',machine])
                self.plan.append(dict(index=len(self.plan),scene_id=f"meeting_{pose_index:02d}_K{requested}",
                    role='PILOT_NOT_TRAIN_CAL_OR_TEST',pose_index=pose_index,pose_id=specs[pose_index]['id'],
                    configured_emitting_points=requested,source_waveform_recipes=kinds,
                    source_positions_m=[s['position_m'] for s in state['sources'][:requested]],
                    seed=p['seed']+len(self.plan)*1009,
                    room_size_m=state['room_size_m'],array_center_m=state['array_center_m'],
                    array_pose=state['config']['array_pose']))

    def prepare_audio(self):
        if self.speech is None:
            self.speech,self.activity_metadata=load_speech(self.assets)
        return self.activity_metadata

    def generate_pose_rirs(self, pose_index):
        runtime=ROOT/'.runtime/rir_generator_0_3_0'
        lock=json.loads((ROOT/'results/candidate_rir_preparation_20261007/protocol_lock.json').read_text(encoding='utf-8'))
        for info in lock['pinned_package'].values():
            assert digest(Path(info['path']))==info['sha256']
        sys.path.insert(0,str(runtime))
        import rir_generator as gen
        assert gen.__version__=='0.3.0'
        state=self.states[pose_index]
        self.rirs=[gen.generate(**rir_arguments(state,s),mtype=gen.mtype.omnidirectional).astype(np.float32)
                   for s in state['sources']]
        assert all(h.shape==(self.H,16) and np.isfinite(h).all() for h in self.rirs)
        self.rir_pose=pose_index
        return self.rirs

    def sample(self, index):
        recipe=self.plan[index];pose_index=recipe['pose_index'];state=self.states[pose_index]
        self.prepare_audio()
        if self.rir_pose!=pose_index:self.generate_pose_rirs(pose_index)
        size=self.H+self.N;seed=recipe['seed'];p=self.pilot
        noise_config=deepcopy(state['config']);noise_config['background']['seed']=seed+1000003
        background=background_from_config(noise_config,self.N);pcm=background.astype(float)
        reference_config=deepcopy(noise_config);reference_config['background']['seed']=seed+7000001
        reference=background_from_config(reference_config,4*2048).reshape(4,2048,16)
        details=[];components=[];masks=[]
        for slot,kind in enumerate(recipe['source_waveform_recipes']):
            rng=np.random.default_rng(seed+slot*100003)
            if kind=='speech':
                asset=self.assets[slot%2];audio,mask=self.speech[asset['sha256']]
                assert len(audio)>=size
                crop=int(rng.integers(0,len(audio)-size+1))
                # 完整素材发声参考与房间和裁剪独立。
                dry=audio[crop:crop+size].astype(float)*p['emitter_digital_rms']/rms(audio)
                emitted=mask[crop:crop+size]
                source_ref=dict(original_asset_sha256=asset['sha256'],crop_start_sample=crop,
                    original_asset_path=asset['path'],emission_reference='complete original dry utterance RMS')
            else:
                axis=np.arange(size)/self.fs
                if kind=='fan':dry=lfilter([1.],[1.,-.91],rng.standard_normal(size))+.2*np.sin(2*np.pi*173*axis)
                else:dry=np.sin(2*np.pi*230*axis)+.35*np.sin(2*np.pi*460*axis)+.08*rng.standard_normal(size)
                dry=dry*p['emitter_digital_rms']/rms(dry);emitted=np.ones(size,bool)
                source_ref=dict(machine_seed=seed+slot*100003,emission_reference='full deterministic dry waveform RMS')
            dry*=10**(p['source_gain_db'][slot]/20)
            h=self.rirs[slot]
            component=fftconvolve(dry[:,None],h,mode='full',axes=0)[self.H:self.H+self.N].astype(np.float32)
            pcm+=component;components.append(component)
            source=state['sources'][slot];delay=source['distance_m']*self.fs/state['config']['room']['sound_speed_m_s']
            indices=np.floor(self.H+np.arange(self.N)-delay+.5).astype(int)
            assert np.all((indices>=0)&(indices<size));active=emitted[indices];masks.append(active)
            details.append(dict(source_id=source['id'],source_slot=slot,
                direction=[source['azimuth_deg'],source['elevation_deg']],position_m=source['position_m'],
                distance_m=source['distance_m'],active_samples=int(active.sum()),
                dry_audio_sha256=array_sha(dry.astype(np.float32)),activity_mask_sha256=array_sha(active),
                component_sha256=array_sha(component),component_rms=rms(component),
                component_to_background_db=20*math.log10(max(rms(component),1e-30)/rms(background)),
                emitted_gain_db=p['source_gain_db'][slot],waveform_kind_audit_only=kind,**source_ref))
        pcm=pcm.astype(np.float32)
        assert np.isfinite(pcm).all() and np.max(np.abs(pcm))<1, 'no automatic amplitude repair or scene reroll'
        return dict(frames=pcm.reshape(20,2048,16),reference=reference,components=components,
            activity_masks=masks,background=background,record=dict(**recipe,source_details=details,
                pcm_sha256=array_sha(pcm),reference_sha256=array_sha(reference),
                background_sha256=array_sha(background),pcm_peak_abs=float(np.max(np.abs(pcm))),
                pcm_rms=rms(pcm),background_rms=rms(background),pcm_saved=False,
                room_generation='NEW_CONFIGURED_RIRS_NO_OLD_MIXTURES_OR_FEATURE_CACHE',
                receiver_level_renormalized=False))
