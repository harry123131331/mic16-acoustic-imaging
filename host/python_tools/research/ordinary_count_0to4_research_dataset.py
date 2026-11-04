"""New 0--4 source development recipes; shared immutable acoustic implementations.

Four-point paired layouts, role-isolated speech, dry-only joint crop eligibility.
Control uses identical background and point components, capped at two on TRAIN.
"""
from copy import deepcopy
import json
import math
from pathlib import Path
import numpy as np
from research.ordinary_meeting_room_research_dataset import OrdinaryRoomResearchDataset
from research.meeting_room_research_dataset import MeetingRoomResearchDataset, ROOT
from research.manual_room_simulation import validate, rotation_matrix
from research.candidate_rescue_synthesis import array_sha
from acoustic_camera.valid_source_detection import angular_distance_deg

CONFIG=ROOT/'config/simulation/meeting_room_count_0to4_research_20261014.json'


class OrdinaryCountResearchDataset(OrdinaryRoomResearchDataset):
    def _world_layout(self,name,dims,rt60,rng):
        p=self.config;g=p['geometry_variation'];base=self.base['config']
        offsets=np.asarray([s['position_m'] for s in base['sources']])-np.asarray(base['array_pose']['center_m'])
        if offsets.shape!=(4,3):raise ValueError('four manual template points required')
        for attempt in range(1,p['maximum_geometry_attempts']+1):
            center=np.r_[dims[:2]*.5+rng.uniform(-g['center_horizontal_jitter_m'],g['center_horizontal_jitter_m'],2),
                         base['array_pose']['center_m'][2]+rng.uniform(-g['center_height_jitter_m'],g['center_height_jitter_m'])]
            yaw=float(rng.uniform(*g['yaw_range_deg']));rotation=rotation_matrix(yaw,0.,0.)
            local=offsets.copy();local[:,:2]*=rng.uniform(*g['source_horizontal_scale_range'])
            local+=rng.uniform(-np.asarray(g['source_position_jitter_m']),np.asarray(g['source_position_jitter_m']),local.shape)
            points=center+local@rotation.T
            shifted=center+np.r_[rng.uniform(-g['second_pose_horizontal_jitter_m'],g['second_pose_horizontal_jitter_m'],2),
                                 rng.uniform(-g['second_pose_height_jitter_m'],g['second_pose_height_jitter_m'])]
            if np.linalg.norm(center[:2]-shifted[:2])<g['second_pose_minimum_translation_m']:continue
            states=[]
            for pose,location in enumerate((center,shifted)):
                config=deepcopy(base);config['scene_id']=f'{name}_count4_pose{pose}'
                config['room']['dimensions_m']=dims.tolist();config['room']['target_rt60_s']=rt60
                config['array_pose']=dict(center_m=location.tolist(),yaw_deg=yaw+(0 if pose==0 else float(rng.uniform(-g['second_pose_yaw_jitter_deg'],g['second_pose_yaw_jitter_deg']))),pitch_deg=0.,roll_deg=0.)
                config['sources']=[dict(id=f'S{j+1}',position_m=point.tolist()) for j,point in enumerate(points)]
                config['rir']['duration_s']=max(p['rir']['minimum_duration_s'],p['rir']['duration_RT60_multiplier']*rt60)
                config['rir']['reflection_order']=p['rir']['reflection_order']
                try:state=validate(config)
                except ValueError:break
                sources=state['sources']
                if not all(s['inside_current_search_FOV'] and p['minimum_source_range_m']<=s['distance_m']<=p['maximum_source_range_m']
                           and p['source_height_range_m'][0]<=s['position_m'][2]<=p['source_height_range_m'][1] for s in sources):break
                minimum=min(angular_distance_deg(a['azimuth_deg'],a['elevation_deg'],b['azimuth_deg'],b['elevation_deg'])
                            for j,a in enumerate(sources) for b in sources[j+1:])
                if minimum<p['minimum_angular_separation_deg']:break
                state.update(actual_background_model=p['background']['model'],minimum_pair_separation_deg=minimum,
                             warnings=['ordinary separated four-point recipe; not evidence of four-source detector performance'])
                states.append(state)
            if len(states)==2:return states,attempt
        raise RuntimeError('bounded geometry construction failed without acoustic screening: '+name)

    def __init__(self,config_path=CONFIG,*,maximum_windows=256,maximum_RIRs=64):
        MeetingRoomResearchDataset.__init__(self,config_path)
        p=self.config;patterns=p['ordinary']['per_pose_kind_patterns']
        assert len(patterns)==16 and len(self.states)==20 and len(self.plan)==320
        assert all(len(s['sources'])==4 for s in self.states)
        for recipe in self.plan:
            i=recipe['index'];local=i%16;state=recipe['state_index'];group=state//2;kinds=patterns[local]
            recipe.update(scene_id=f'count4_{recipe["role"].lower()}_g{group:02d}_p{recipe["pose_index"]}_w{local:02d}_K{len(kinds)}',
                          configured_emitting_points=len(kinds),source_recipes=[])
            slots=np.roll(np.arange(4),-(local+state)%4)[:len(kinds)].tolist()
            assets=self.asset_groups[recipe['role']];speakers=sorted({a['speaker_id'] for a in assets});speech_count=0
            if len(speakers)<2:raise ValueError('atleast two distinct speakers per role')
            target=p['ordinary']['receiver_window_SBR_db'][(local+state)%3]
            for n,(kind,slot) in enumerate(zip(kinds,slots)):
                seed=p['seed']+1009*(i+1)+slot*100003
                src=dict(slot=slot,kind=kind,seed=seed,gain_db=0.,target_receiver_window_SBR_db=float(target-(5 if target==20 and n%2 else 0)))
                if kind=='speech':
                    speaker=speakers[(group*3+local+speech_count)%len(speakers)];speech_count+=1
                    subset=[a for a in assets if a['speaker_id']==speaker];asset=subset[(state+local+n)%len(subset)]
                    src.update(asset_sha256=asset['sha256'],asset_path=asset['path'],speaker_id_audit_only=speaker,crop_seed=seed+80000003)
                recipe['source_recipes'].append(src)
            assert speech_count<=2
            selection=np.sort(np.random.default_rng(p['seed']+i*7919+500000003).choice(len(kinds),min(2,len(kinds)),replace=False)).tolist()
            recipe['TRAIN_control_source_recipe_indices']=selection if recipe['role']=='TRAIN' else None
        self.maximum_windows=maximum_windows;self.maximum_RIRs=maximum_RIRs
        self.RIRs_generated=0;self.windows_generated=set();self.generated_states=set()
        self.crop_audit={};self.crops_ready=set()

    def prepare_source_crops(self,role):
        if role not in ('TRAIN','CAL'):raise ValueError('RESERVE dry audio and crops remain sealed')
        if role in self.crops_ready:return self.crop_audit[role]
        self.prepare_audio(role);p=self.config['ordinary'];minimum=math.ceil(p['minimum_active_duration_s']*self.fs)
        overlap_min=math.ceil(p['minimum_all_source_overlap_s']*self.fs);hop=round(p['speech_crop_grid_s']*self.fs)
        prefixes={};audit=[]
        for recipe in self.plan:
            if recipe['role']!=role:continue
            state=self.states[recipe['state_index']];H=state['sample_count'];size=H+self.N;common=np.ones(self.N,bool)
            for src in recipe['source_recipes']:
                if src['kind']!='speech':continue
                sha=src['asset_sha256'];audio,mask=self.speech[sha]
                if len(audio)<size:raise ValueError('fixed asset too short,stop without asset swap')
                if sha not in prefixes:prefixes[sha]=np.r_[0,np.cumsum(mask,dtype=np.int64)]
                delay=state['sources'][src['slot']]['distance_m']*self.fs/343.;offset=math.floor(H-delay+.5)
                candidates=np.arange(0,len(audio)-size+1,hop,dtype=np.int64)
                counts=prefixes[sha][candidates+offset+self.N]-prefixes[sha][candidates+offset]
                eligible=np.flatnonzero(counts>=minimum);joint=[]
                for j in eligible:
                    begin=int(candidates[j])+offset
                    if np.count_nonzero(common&mask[begin:begin+self.N])>=overlap_min:joint.append(int(j))
                if not joint:raise ValueError('no fixed-asset dry crop meetsjoint activity; no acoustic retry or asset swap')
                chosen=joint[int(np.random.default_rng(src['crop_seed']).integers(len(joint)))];crop=int(candidates[chosen])
                direct=mask[crop+offset:crop+offset+self.N];assert int(direct.sum())==int(counts[chosen]);common&=direct
                src.update(crop_start_sample=crop,expected_active_samples=int(direct.sum()))
                audit.append(dict(index=recipe['index'],role=role,source_slot=src['slot'],asset_sha256=sha,
                    individual_eligible_crops=len(eligible),joint_eligible_crops=len(joint),crop_start_sample=crop,
                    activity_mask_sha256=array_sha(direct),active_samples=int(direct.sum()),common_active_samples=int(common.sum())))
            recipe['expected_all_source_overlap_samples']=int(common.sum()) if recipe['source_recipes'] else 0
        self.crop_audit[role]=audit;self.crops_ready.add(role);return audit

    def generate_state_rirs(self,index):
        if type(index) is not int or not 0<=index<len(self.states) or self.states[index]['research_role']=='RESERVE':
            raise ValueError('invalid or sealed RIR state')
        if self.rir_state==index:return self.rirs
        if index in self.generated_states:raise ValueError('no automatic regeneration of discarded RIRs within this protocol')
        if self.RIRs_generated+4>self.maximum_RIRs:raise ValueError('finite RIR budget exhausted')
        result=MeetingRoomResearchDataset.generate_state_rirs(self,index)
        self.RIRs_generated+=len(result);self.generated_states.add(index);return result

    def sample(self,index):
        if type(index) is not int or not 0<=index<256:raise ValueError('invalid or sealed sample')
        if index in self.windows_generated or len(self.windows_generated)>=self.maximum_windows:
            raise ValueError('finite unique window budget exhausted;no replay')
        sample=OrdinaryRoomResearchDataset.sample(self,index)
        masks=sample['activity_masks'];overlap=int(np.logical_and.reduce(masks).sum()) if masks else 0
        assert overlap==self.plan[index]['expected_all_source_overlap_samples']
        if masks and overlap<math.ceil(self.config['ordinary']['minimum_all_source_overlap_s']*self.fs):raise ValueError('actual joint activity failed')
        sample['record'].update(all_source_overlap_samples=overlap,protocol_version='ordinary_count_0to4_research_20261014')
        self.windows_generated.add(index);return sample


def cap_TRAIN_components(sample):
    """Exact TRAIN-only intervention, independent of kind, truth or network score."""
    record=sample['record']
    if record['role']!='TRAIN':raise ValueError('CAL must use the same full mixture in both branches')
    selected=record['TRAIN_control_source_recipe_indices']
    assert selected is not None and len(selected)==min(2,len(sample['components']))
    total=sample['background'].astype(float).copy()
    for j in selected:total+=sample['components'][j]
    pcm=total.astype(np.float32);r=deepcopy(record)
    r.update(source_recipes=[r['source_recipes'][j] for j in selected],source_details=[r['source_details'][j] for j in selected],
             configured_emitting_points=len(selected),control_parent_PCM_sha256=record['pcm_sha256'],
             retained_source_recipe_indices=selected,pcm_sha256=array_sha(pcm),pcm_peak_abs=float(np.max(np.abs(pcm))),
             protocol_version='count_source_cap2_TRAIN_control_20261014')
    masks=[sample['activity_masks'][j] for j in selected]
    r['all_source_overlap_samples']=int(np.logical_and.reduce(masks).sum()) if masks else 0
    r['expected_all_source_overlap_samples']=r['all_source_overlap_samples']
    if len(sample['components'])<=2:
        assert np.array_equal(pcm,sample['frames'].reshape(40960,16))
    return dict(frames=pcm.reshape(20,2048,16),reference=sample['reference'],background=sample['background'],
                components=[sample['components'][j] for j in selected],activity_masks=masks,record=r)
