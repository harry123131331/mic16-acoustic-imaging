"""Frozen PC prototype: complete C16 scoring followed by one-new-source memory.

The release manifest binds weights and Python dependencies. No source category,
truth position or scene source count is accepted. No masked rescan/residual gate.
"""
import hashlib
import json
import math
from pathlib import Path
import numpy as np
import torch
from research.candidate_subband_competition import CompetitionNetwork
from research.incremental_source_search import IncrementalSearchMemory
from research.meeting_room_detection_features import MeetingRoomFeatures
from research.retained_pc_detector import RetainedPCDetector, ContiguousPCStream, FS, DURATION

ROOT=Path(__file__).resolve().parents[2]
MANIFEST=ROOT/'release/pc_incremental_c16_frozen_20261015/freeze_manifest.json'
VERSION='MIC16_PC_C16_INCREMENTAL_FP32_V1_20261015'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def consume_scored_cycle(memory, directions, valid, scores, *, audio_end_s, pose_key, contiguous):
    """Same observed eligibility and memory transition as retained control16."""
    d=np.asarray(directions,float);v=np.asarray(valid);s=np.asarray(scores,float)
    if d.shape!=(16,2) or v.shape!=(16,) or s.shape!=(16,):
        raise ValueError('fixed full C16 candidate shapes required')
    if not np.isfinite(d).all() or not np.isfinite(s).all() or not np.isin(v,[0,1]).all() or ((s<0)|(s>1)).any():
        raise ValueError('finite directions/probability-range scores and boolean validity required')
    candidates=[]
    for j in np.flatnonzero(v):
        a,e=map(float,d[j]);score=float(s[j])
        if score<.65 or abs(a)>60+1e-9 or abs(e)>30+1e-9:
            continue
        candidates.append(dict(direction=[float(np.clip(a,-60,60)),float(np.clip(e,-30,30))],score=score,candidate_index=int(j)))
    return memory.step(new_candidates=candidates,known_observations=candidates,
        audio_end_s=audio_end_s,pose_key=pose_key,contiguous=contiguous)


class _FrozenWindowDetector(RetainedPCDetector):
    def __init__(self, manifest):
        self.manifest=dict(weights_sha256=manifest['checkpoint_sha256'])
        self.model=CompetitionNetwork().cpu().eval()
        self.model.load_state_dict(torch.load(ROOT/manifest['frozen_checkpoint'],map_location='cpu',weights_only=True)['state_dict'],strict=True)
        self.model.requires_grad_(False)
        if sum(p.numel() for p in self.model.parameters())!=29441:
            raise ValueError('frozen parameter count mismatch')
        self.frontend=MeetingRoomFeatures();self.audit_capture=False;self.last_observed=None;self.forward_calls=0


class FrozenIncrementalPCDetector:
    """Whole-window input; IDs belong to one uninterrupted pose/reference segment."""
    def __init__(self, *, pose_key='fixed_array_pose', manifest_path=MANIFEST):
        if not isinstance(pose_key,str) or not pose_key:
            raise ValueError('explicit nonempty array pose key required')
        manifest=json.loads(Path(manifest_path).read_text(encoding='utf-8'))
        if manifest['version']!=VERSION or manifest['score_threshold']!=.65 or manifest['new_source_exclusion_deg']!=15.:
            raise ValueError('frozen model contract mismatch')
        for name,h in manifest['live_source_sha256'].items():
            if digest(ROOT/name)!=h:raise ValueError('frozen dependency changed: '+name)
        if digest(ROOT/manifest['frozen_checkpoint'])!=manifest['checkpoint_sha256']:
            raise ValueError('frozen checkpoint changed')
        self.manifest=manifest;self.engine=_FrozenWindowDetector(manifest);self.pose_key=pose_key
        self.reset_segment()

    def reset_segment(self):
        self.memory=IncrementalSearchMemory();self.last_audio_end=None

    def infer_window(self,pcm,quiet_reference,*,sample_rate,audio_start_s,audio_end_s,data_origin,contiguous):
        if contiguous is not True or (self.last_audio_end is not None and (not math.isfinite(audio_start_s) or abs(audio_start_s-self.last_audio_end)>1e-6)):
            self.reset_segment();raise ValueError('audio discontinuity: memory cleared; restart contiguous segment')
        try:
            scored=self.engine.infer_window(pcm,quiet_reference,sample_rate=sample_rate,audio_start_s=audio_start_s,
                audio_end_s=audio_end_s,data_origin=data_origin,contiguous=contiguous)
            # 参考变化与姿态键变化一样，都重置源记忆。
            reference_key=hashlib.sha256(np.asarray(quiet_reference,np.float32).reshape(-1,16).tobytes()).hexdigest()
            state=consume_scored_cycle(self.memory,scored['observed_directions'],scored['candidate_valid'],scored['candidate_scores'],
                audio_end_s=audio_end_s,pose_key=self.pose_key+':'+data_origin+':'+reference_key,contiguous=True)
        except Exception:
            self.reset_segment();raise
        self.last_audio_end=float(audio_end_s)
        return dict(contract_version=VERSION,status='FROZEN_PC_PROTOTYPE_NOT_BOARD_ACCEPTANCE',
            data_origin=data_origin,board_live=False,checkpoint_sha256=self.manifest['checkpoint_sha256'],
            audio_interval_s=[audio_start_s,audio_end_s],parameters=29441,sample_rate_hz=FS,
            tracks=state['tracks'],new_source=state['new_source'],expired_source_ids=state['expired_source_ids'],
            current_active_count=state['confirmed_this_cycle_count'],recent_track_count=state['recently_localized_count'],
            held_tracks_are_not_current_activity=True,all_scene_sources_found=False,
            one_new_source_per_cycle=True,source_kind=None,heatmap=scored['heatmap'],
            observed_directions=scored['observed_directions'],candidate_valid=scored['candidate_valid'],
            candidate_logits=scored['candidate_logits'],candidate_scores=scored['candidate_scores'],
            scorer_timings_s=scored['timings_s'],incremental_online_timing_not_measured=True,
            observation_wait_s=DURATION,continuous_realtime_certified=False)


class FrozenPCStream(ContiguousPCStream):
    """Reuse checked one-window backpressure and clear memory on any input failure."""
    def restart(self,*,start_sample,start_time_s):
        super().restart(start_sample=start_sample,start_time_s=start_time_s)
        self.engine.reset_segment()

    def push(self,*args,**kwargs):
        try:outputs=super().push(*args,**kwargs)
        except Exception:
            self.engine.reset_segment();raise
        for output in outputs:output['stream_window']['cross_window_source_tracking']=True
        return outputs
