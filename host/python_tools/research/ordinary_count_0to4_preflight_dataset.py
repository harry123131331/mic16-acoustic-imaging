"""Finite 0..4-source data precheck; reuse original acoustic sampler unchanged.

Four registered points, two array poses, eight TRAIN windows. Crop eligibility
uses dry activity only, including all-source overlap; never acoustic scores.
"""
from copy import deepcopy
from pathlib import Path
import json
import math
import numpy as np
from research.ordinary_meeting_room_research_dataset import OrdinaryRoomResearchDataset
from research.meeting_room_research_dataset import MeetingRoomResearchDataset,ROOT,SPEECH_LOCK
from research.manual_room_simulation import load_config,validate,digest
from research.spatial_diffuse_background import SpatialBackground
from research.candidate_rescue_synthesis import array_sha
from acoustic_camera.valid_source_detection import angular_distance_deg

CONFIG=ROOT/'config/simulation/meeting_room_count_0to4_preflight_20261014.json'


class OrdinaryCountPreflightDataset(OrdinaryRoomResearchDataset):
    def __init__(self,config_path=CONFIG):
        self.config_path=Path(config_path);self.config=json.loads(self.config_path.read_text(encoding='utf-8'))
        p=self.config
        if p['schema_version']!=1 or p['window_samples']!=40960 or len(p['windows'])!=8 or len(p['array_poses'])!=2:
            raise ValueError('This finite profile requires eight windows and two poses')
        self.base=load_config(ROOT/p['manual_room_config']);self.fs=39062.5;self.N=40960
        if len(self.base['config']['sources'])!=4 or self.base['config']['rir']['sample_rate_hz']!=self.fs:
            raise ValueError('four registered positions and39062.5Hz required')
        self.states=[]
        for index,pose in enumerate(p['array_poses']):
            config=deepcopy(self.base['config']);config['scene_id']=f'ordinary_count_0to4_pose{index}'
            config['array_pose']={k:pose[k] for k in ('center_m','yaw_deg','pitch_deg','roll_deg')}
            config['room']['target_rt60_s']=pose['target_rt60_s'];state=validate(config)
            src=state['sources']
            if not all(s['inside_current_search_FOV'] and p['minimum_source_range_m']<=s['distance_m']<=p['maximum_source_range_m'] for s in src):
                raise ValueError('registered sources violate FOV or distance bounds')
            if not all(p['source_height_range_m'][0]<=s['position_m'][2]<=p['source_height_range_m'][1] for s in src):
                raise ValueError('source height violates ordinaryprofile')
            minimum=min(angular_distance_deg(a['azimuth_deg'],a['elevation_deg'],b['azimuth_deg'],b['elevation_deg']) for j,a in enumerate(src) for b in src[j+1:])
            if minimum<p['minimum_angular_separation_deg']:raise ValueError('source separation too small')
            state.update(research_role='TRAIN',actual_background_model=p['background']['model'],
                minimum_pair_separation_deg=minimum,warnings=['four registered positions;data QA only,currentdetector not certified forfour sources'])
            self.states.append(state)
        assert self.states[0]['config']['sources']==self.states[1]['config']['sources']
        rows=json.loads(SPEECH_LOCK.read_text(encoding='utf-8'))['speech_assets']
        self.asset_groups={'TRAIN':[a for a in rows if a['role']=='TRAIN' and a['speaker_id'] not in {'116','700'}]}
        assets=self.asset_groups['TRAIN'];speakers=sorted({a['speaker_id'] for a in assets})
        if len(speakers)<2:raise ValueError('distinct TRAIN speakers required')
        for asset in assets:
            if digest(Path(asset['path']))!=asset['sha256']:raise ValueError('source asset hash mismatch')
        self.plan=[]
        for index,design in enumerate(p['windows']):
            kinds=design['kinds'];slots=design['source_slots'];levels=design['receiver_window_SBR_db']
            if not len(kinds)==len(slots)==len(levels)<=4 or len(set(slots))!=len(slots) or any(j not in range(4) for j in slots):
                raise ValueError('one unique physical point per emitting source')
            if levels and (min(levels)<10 or max(levels)>20 or max(levels)-min(levels)>p['ordinary']['maximum_receiver_source_level_difference_db']):
                raise ValueError('ordinary receiver SBR bounds violated')
            if design['pose'] not in (0,1):raise ValueError('invalid pose')
            recipe=dict(index=index,scene_id=f'count_preflight_p{design["pose"]}_w{index:02d}_K{len(kinds)}',
                role='TRAIN',state_index=design['pose'],pose_index=design['pose'],configured_emitting_points=len(kinds),
                background_seed=p['seed']+1009*(index+1)+1000003,reference_seed=p['seed']+1009*(index+1)+7000001,
                background_rms=10**(p['background_rms_dbfs']/20),source_recipes=[])
            speech_number=0
            for kind,slot,level in zip(kinds,slots,levels):
                if kind not in ('speech','fan','motor'):raise ValueError('unsupported dry waveform recipe')
                seed=p['seed']+1009*(index+1)+slot*100003
                source=dict(slot=slot,kind=kind,seed=seed,gain_db=0.,target_receiver_window_SBR_db=float(level))
                if kind=='speech':
                    speaker=speakers[(index+speech_number)%len(speakers)];speech_number+=1
                    subset=[a for a in assets if a['speaker_id']==speaker];asset=subset[(design['pose']+index)%len(subset)]
                    source.update(asset_sha256=asset['sha256'],asset_path=asset['path'],speaker_id_audit_only=speaker,crop_seed=seed+80000003)
                recipe['source_recipes'].append(source)
            if speech_number>1:
                assert len({s['speaker_id_audit_only'] for s in recipe['source_recipes'] if s['kind']=='speech'})==speech_number
            self.plan.append(recipe)
        self.speech={};self.activity_metadata={};self.crop_audit={};self.crops_ready=set()
        self.rirs=None;self.rir_state=None;self.RIRs_generated=0;self.windows_generated=set()
        self.background=SpatialBackground(self.states[0]['microphone_positions_m'],
            diffuse_fraction=p['background']['diffuse_power_fraction'],ar1=p['background']['ar1_coefficient'],fft_size=p['background']['fft_size'])

    def prepare_audio(self,role):
        if role!='TRAIN':raise ValueError('thisprecheck cannotdecode CAL orRESERVE audio')
        return MeetingRoomResearchDataset.prepare_audio(self,role)

    def generate_state_rirs(self,index):
        if type(index) is not int or index not in (0,1):raise ValueError('only twoTRAIN poses available')
        if self.rir_state==index:return self.rirs
        if self.RIRs_generated+4>self.config['budget']['RIRs_max']:raise ValueError('finite8RIR generation budget exhausted')
        result=MeetingRoomResearchDataset.generate_state_rirs(self,index);self.RIRs_generated+=len(result)
        return result

    def prepare_source_crops(self,role):
        if role!='TRAIN':raise ValueError('only TRAIN dry activity may be used')
        if role in self.crops_ready:return self.crop_audit[role]
        self.prepare_audio(role);p=self.config['ordinary'];minimum=math.ceil(p['minimum_active_duration_s']*self.fs)
        overlap_minimum=math.ceil(p['minimum_all_source_overlap_s']*self.fs);hop=round(p['speech_crop_grid_s']*self.fs)
        prefixes={};audit=[]
        for recipe in self.plan:
            state=self.states[recipe['state_index']];H=state['sample_count'];size=H+self.N;common=np.ones(self.N,bool)
            for src in recipe['source_recipes']:
                if src['kind']!='speech':continue
                sha=src['asset_sha256'];audio,mask=self.speech[sha]
                if len(audio)<size:raise ValueError('fixed source asset too short')
                if sha not in prefixes:prefixes[sha]=np.r_[0,np.cumsum(mask,dtype=np.int64)]
                delay=state['sources'][src['slot']]['distance_m']*self.fs/343.;offset=math.floor(H-delay+.5)
                candidates=np.arange(0,len(audio)-size+1,hop,dtype=np.int64)
                counts=prefixes[sha][candidates+offset+self.N]-prefixes[sha][candidates+offset]
                eligible=np.flatnonzero(counts>=minimum)
                joint=[]
                for j in eligible:
                    begin=int(candidates[j])+offset
                    if np.count_nonzero(common&mask[begin:begin+self.N])>=overlap_minimum:joint.append(int(j))
                if not joint:raise ValueError('no fixed-asset crop meetsordinary jointactivity;stop without swappingasset or acoustic retry')
                chosen=joint[int(np.random.default_rng(src['crop_seed']).integers(len(joint)))];crop=int(candidates[chosen])
                direct=mask[crop+offset:crop+offset+self.N];assert int(direct.sum())==int(counts[chosen])
                common&=direct;src.update(crop_start_sample=crop,expected_active_samples=int(direct.sum()))
                audit.append(dict(index=recipe['index'],source_slot=src['slot'],asset_sha256=sha,
                    speaker_id_audit_only=src['speaker_id_audit_only'],candidate_crops=len(candidates),
                    individual_eligible_crops=len(eligible),joint_eligible_crops=len(joint),crop_start_sample=crop,
                    activity_mask_sha256=array_sha(direct),active_samples=int(direct.sum()),common_active_samples=int(common.sum()),
                    selection='dry activity andanalyticarrivalonly;noRIR/spatialmap/modelscore used'))
            recipe['expected_all_source_overlap_samples']=int(common.sum()) if recipe['source_recipes'] else 0
        self.crop_audit[role]=audit;self.crops_ready.add(role);return audit

    def sample(self,index):
        if type(index) is not int or index not in range(8):raise ValueError('onlyeight registeredTRAIN windows available')
        if index in self.windows_generated:raise ValueError('no automatic replay of a precheck window')
        sample=OrdinaryRoomResearchDataset.sample(self,index)
        masks=sample['activity_masks'];overlap=int(np.logical_and.reduce(masks).sum()) if masks else 0
        assert overlap==self.plan[index]['expected_all_source_overlap_samples']
        if masks and overlap<math.ceil(self.config['ordinary']['minimum_all_source_overlap_s']*self.fs):
            raise ValueError('actualdirectaligned jointactivity failed;stop')
        sample['record']['all_source_overlap_samples']=overlap
        sample['record']['protocol_version']='ordinary_count_0to4_preflight_20261014'
        self.windows_generated.add(index)
        return sample
