"""Ordinary indoor pilot: fixed recipes, receiver SBR control, no model calls.

The historical generator is inherited unchanged. Per-source scalar gains
are equivalent to emitter gains before a linear RIR, not mixture normalization.
"""
from copy import deepcopy
from pathlib import Path
import math
import numpy as np
from research.meeting_room_research_dataset import MeetingRoomResearchDataset, ROOT
from research.manual_room_simulation import validate
from research.candidate_rescue_synthesis import rms, array_sha
from acoustic_camera.valid_source_detection import angular_distance_deg

CONFIG = ROOT/'config/simulation/meeting_room_ordinary_pilot_20261012.json'


class OrdinaryMeetingRoomDataset(MeetingRoomResearchDataset):
    def _world_layout(self, name, dims, rt60, rng):
        p = self.config
        for attempt in range(1, p['maximum_geometry_attempts']+1):
            positions = np.column_stack([rng.uniform(.18,.82,(2,2))*dims[:2], rng.uniform(.7,1.8,2)])
            center = np.r_[rng.uniform(.12,.88,2)*dims[:2],rng.uniform(.9,1.4)]
            shifted = center+np.r_[rng.uniform(-.9,.9,2),rng.uniform(-.15,.15)]
            if np.linalg.norm(center[:2]-shifted[:2]) < .4:
                continue
            states=[]
            for pose, location in enumerate((center,shifted)):
                config=deepcopy(self.base['config'])
                config['scene_id']=f'{name}_ordinary_pose{pose}'
                config['room']['dimensions_m']=dims.tolist()
                config['room']['target_rt60_s']=rt60
                config['array_pose']['center_m']=location.tolist()
                delta=positions.mean(axis=0)-location
                config['array_pose']['yaw_deg']=float(np.rad2deg(np.arctan2(-delta[0],delta[1])))
                config['sources']=[dict(id=f'S{j+1}',position_m=x.tolist()) for j,x in enumerate(positions)]
                config['rir']['duration_s']=max(p['rir']['minimum_duration_s'],p['rir']['duration_RT60_multiplier']*rt60)
                config['rir']['reflection_order']=p['rir']['reflection_order']
                try:
                    state=validate(config)
                except ValueError:
                    break
                s=state['sources']
                if not all(x['inside_current_search_FOV'] and p['minimum_source_range_m']<=x['distance_m']<=p['maximum_source_range_m'] for x in s):
                    break
                if angular_distance_deg(s[0]['azimuth_deg'],s[0]['elevation_deg'],s[1]['azimuth_deg'],s[1]['elevation_deg']) < p['minimum_angular_separation_deg']:
                    break
                state['actual_background_model']=p['background']['model']
                state['warnings']=[w for w in state['warnings'] if '独立AR' not in w]
                states.append(state)
            if len(states)==2:
                return states,attempt
        raise RuntimeError('bounded ordinary geometry construction failed: '+name)

    def __init__(self, config_path=CONFIG):
        super().__init__(config_path)
        assert len(self.states)==2 and all(s['research_role']=='TRAIN' for s in self.states)
        parent_plan=self.plan
        self.plan=[]
        for i, design in enumerate(self.config['ordinary_pilot']['windows']):
            pose=design['pose'];base=deepcopy(parent_plan[pose*16+2])
            base.update(index=i,scene_id=f'ordinary_p{pose}_w{i:02d}_K{len(design["kinds"])}',
                variant=i%4,configured_emitting_points=len(design['kinds']),
                background_seed=self.config['seed']+1009*(i+1)+1000003,
                reference_seed=self.config['seed']+1009*(i+1)+7000001)
            speech_recipe=deepcopy(base['source_recipes'][0]);base['source_recipes']=[]
            assert len(design['kinds'])==len(design['receiver_window_SBR_db'])<=2
            if design['receiver_window_SBR_db']:
                assert min(design['receiver_window_SBR_db'])>=10
                assert max(design['receiver_window_SBR_db'])-min(design['receiver_window_SBR_db'])<=6
            slots=design.get('source_slots',list(range(len(design['kinds']))))
            assert len(slots)==len(design['kinds']) and len(set(slots))==len(slots) and all(slot in (0,1) for slot in slots)
            for slot, kind, sbr in zip(slots, design['kinds'], design['receiver_window_SBR_db']):
                assert kind in ('speech','fan','motor')
                src=deepcopy(speech_recipe) if kind=='speech' else {}
                src.update(slot=slot,kind=kind,seed=self.config['seed']+1009*(i+1)+slot*100003,
                    gain_db=0.,target_receiver_window_SBR_db=float(sbr))
                base['source_recipes'].append(src)
            self.plan.append(base)
        assert len(self.plan)==8

    def sample(self, index):
        sample=super().sample(index)
        record=sample['record'];background=sample['background'];scaled=[]
        assert all(int(m.sum())/self.fs >= self.config['ordinary_pilot']['minimum_active_duration_s'] for m in sample['activity_masks']), 'short ordinary emission: stop, no crop reselection'
        original_pcm_sha=record['pcm_sha256']
        for recipe, detail, component in zip(record['source_recipes'],record['source_details'],sample['components']):
            target=recipe['target_receiver_window_SBR_db']
            factor=rms(background)*10**(target/20)/rms(component)
            assert np.isfinite(factor) and factor>0
            adjusted=(component.astype(float)*factor).astype(np.float32)
            measured=20*math.log10(rms(adjusted)/rms(background))
            assert abs(measured-target)<1e-5
            detail.update(parent_unadjusted_component_sha256=detail['component_sha256'],
                parent_unadjusted_SBR_db=detail['component_to_background_db'],
                emitter_gain_multiplier=factor,additional_emitter_gain_db=20*math.log10(factor),
                component_sha256=array_sha(adjusted),component_rms=rms(adjusted),
                component_to_background_db=measured,target_receiver_window_SBR_db=target)
            scaled.append(adjusted)
        total=background.astype(float).copy()
        for component in scaled:
            total+=component
        pcm=total.astype(np.float32)
        assert np.isfinite(pcm).all() and np.max(np.abs(pcm))<1, 'clipping: stop, no global renormalization'
        record.update(parent_unadjusted_pcm_sha256=original_pcm_sha,pcm_sha256=array_sha(pcm),
            pcm_peak_abs=float(np.max(np.abs(pcm))),receiver_level_renormalized=False,
            per_source_receiver_SBR_control=True,protocol_version='ordinary_pilot_20261012')
        sample.update(frames=pcm.reshape(20,2048,16),components=scaled)
        return sample
