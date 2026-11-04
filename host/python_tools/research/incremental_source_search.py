"""Isolated incremental-search memory and pre-argmax spatial exclusion helpers.

No audio, class, true count, truth position, neural inference or model mutation.
Caller must supply fresh observed confirmations separately from masked search.
These helpers are not yet integrated into the retained acoustic front end.
"""
from copy import deepcopy
import math
import numpy as np

VERSION='incremental_source_search_prototype_20261014'


def points(value):
    x=np.asarray(value,float).reshape(-1,2)
    if not np.isfinite(x).all() or (np.abs(x[:,0])>60).any() or (np.abs(x[:,1])>30).any():
        raise ValueError('finite az/el within currentsearchFOV required')
    return x


def angle_matrix(a,b):
    def unit(x):
        az,el=np.deg2rad(points(x)).T
        return np.c_[np.cos(el)*np.sin(az),np.sin(el),np.cos(el)*np.cos(az)]
    return np.rad2deg(np.arccos(np.clip(unit(a)@unit(b).T,-1,1)))


def unexcluded_grid_mask(grid,localized_directions,radius_deg=15.):
    """Mask the search grid BEFORE choosing a TF response maximum.

    This excludes a direction cone. It does not subtract a source waveform,
    remove its sidelobes/reflections, or create evidence for a missing source.
    """
    if not 0<radius_deg<=30:raise ValueError('bounded exclusion radius required')
    g=points(grid);known=points(localized_directions)
    if not len(known):return np.ones(len(g),bool)
    return np.all(angle_matrix(g,known)>radius_deg+1e-9,axis=1)


def masked_argmax(values,allowed):
    x=np.asarray(values,float);mask=np.asarray(allowed,bool)
    if x.ndim!=1 or mask.shape!=x.shape or not np.isfinite(x).all():raise ValueError('finiteflatresponse andmatchingmask required')
    if not mask.any():return None
    return int(np.argmax(np.where(mask,x,-np.inf)))


class IncrementalSearchMemory:
    def __init__(self,*,threshold=.65,radius_deg=15.,hold_cycles=3,capacity=8):
        if threshold!=.65 or radius_deg!=15. or type(hold_cycles) is not int or hold_cycles<1 or type(capacity) is not int or not 1<=capacity<=8:
            raise ValueError('fixed prototype .65/15degree andboundedhold/capacity')
        self.threshold=threshold;self.radius_deg=radius_deg;self.hold_cycles=hold_cycles;self.capacity=capacity
        self.tracks=[];self.cycle=-1;self.last_audio_end=None;self.pose_key=None;self.next_id=1

    def search_mask(self,grid):
        return unexcluded_grid_mask(grid,[t['direction'] for t in self.tracks],self.radius_deg)

    def step(self,*,new_candidates,known_observations,audio_end_s,pose_key,contiguous):
        if not isinstance(pose_key,str) or not pose_key or not math.isfinite(audio_end_s):raise ValueError('pose/reference key andfiniteclockrequired')
        if self.last_audio_end is not None and audio_end_s<=self.last_audio_end:raise ValueError('duplicated orreversed cycleclock')
        changed_pose=self.pose_key is not None and pose_key!=self.pose_key
        reset=changed_pose or contiguous is not True
        if reset:self.tracks=[]
        self.cycle+=1;self.last_audio_end=audio_end_s;self.pose_key=pose_key
        if contiguous is not True:
            return dict(status='GAP_RESET_NO_ACCEPTANCE',tracks=[],new_source=None,pose_reset=changed_pose)
        def valid_observations(items):
            rows=[]
            for item in items:
                direction=points([item['direction']])[0].tolist();score=float(item['score'])
                if not math.isfinite(score) or not 0<=score<=1:raise ValueError('observed score mustbein0..1')
                if score>=self.threshold:rows.append(dict(direction=direction,score=score))
            return rows
        known=valid_observations(known_observations);search=valid_observations(new_candidates)
        # 一次确认不能刷新多个轨迹，先分配角度最近者。
        edges=sorted((float(angle_matrix([t['direction']],[o['direction']])[0,0]),ti,oi)
                     for ti,t in enumerate(self.tracks) for oi,o in enumerate(known))
        used_t=set();used_o=set()
        for distance,ti,oi in edges:
            if distance<=self.radius_deg and ti not in used_t and oi not in used_o:
                self.tracks[ti].update(direction=known[oi]['direction'],last_seen_cycle=self.cycle,score=known[oi]['score'])
                used_t.add(ti);used_o.add(oi)
        expired=[t['source_id'] for t in self.tracks if self.cycle-t['last_seen_cycle']>=self.hold_cycles]
        self.tracks=[t for t in self.tracks if t['source_id'] not in expired]
        novel=[o for o in search if not len(self.tracks) or unexcluded_grid_mask([o['direction']],[t['direction'] for t in self.tracks],self.radius_deg)[0]]
        novel.sort(key=lambda o:(-o['score'],o['direction'][0],o['direction'][1]))
        new=None
        if novel and len(self.tracks)<self.capacity:
            observation=novel[0];new=dict(source_id=self.next_id,direction=observation['direction'],score=observation['score'],
                                       first_seen_cycle=self.cycle,last_seen_cycle=self.cycle)
            self.next_id+=1;self.tracks.append(new)
        output=deepcopy(self.tracks)
        for t in output:t['confirmed_this_cycle']=t['last_seen_cycle']==self.cycle
        return dict(status='INCREMENTAL_SEARCH_PROTOTYPE_NOT_ACOUSTICALLY_VERIFIED',cycle=self.cycle,tracks=output,
                    new_source=deepcopy(new),expired_source_ids=expired,pose_reset=changed_pose,
                    recently_localized_count=len(output),confirmed_this_cycle_count=sum(t['confirmed_this_cycle'] for t in output),
                    all_scene_sources_found=False,held_tracks_are_not_proof_of_current_activity=True)
