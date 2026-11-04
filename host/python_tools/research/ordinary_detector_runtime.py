"""PC research inference adapter for the retained ordinary-room detector.

An interface prototype, not a release promotion or completed live GUI link.
Audio interval times are supplied by the transport; they are not video times.
"""
from pathlib import Path
import hashlib
import time
import math
import numpy as np
import torch
from research.meeting_room_detection_features import MeetingRoomFeatures
from research import all_source_detection as task

ROOT=Path(__file__).resolve().parents[2]
CHECKPOINT=ROOT/'results/ordinary_meeting_room_baseline_one_run_20261012/best_checkpoint.pt'
CHECKPOINT_SHA='c92779c7d12f7fea754457cbfbd98e4aa31ce8f268c5e9685b85674cab8f6894'
VERSION='ordinary_C16_PC_runtime_interface_20261013'


class OrdinaryDetectorRuntime:
    def __init__(self):
        if hashlib.sha256(CHECKPOINT.read_bytes()).hexdigest()!=CHECKPOINT_SHA:
            raise ValueError('retained ordinary-room checkpoint hash changed')
        checkpoint=torch.load(CHECKPOINT,map_location='cpu',weights_only=True)
        self.model=task.DetectionNetwork().cpu().eval()
        self.model.load_state_dict(checkpoint['state_dict'],strict=True)
        self.model.requires_grad_(False)
        self.frontend=MeetingRoomFeatures()
        if sum(p.numel() for p in self.model.parameters())!=28673:
            raise ValueError('unexpected detector architecture')

    def score_observed_candidates(self,features,directions,valid):
        x=np.asarray(features,np.float32);d=np.asarray(directions,np.float32);v=np.asarray(valid)
        if x.shape!=(16,20,106) or d.shape!=(16,2) or v.shape!=(16,) or v.dtype!=np.bool_:
            raise ValueError('features[16,20,106], directions[16,2], boolean valid[16] required')
        if not np.isfinite(x).all() or not np.isfinite(d).all():
            raise ValueError('nonfinite observed features/directions')
        with torch.inference_mode():
            logits=self.model(torch.from_numpy(x[None]),torch.from_numpy(d[None]),torch.from_numpy(v[None]))[0].numpy()
        # 使用与保留证据评估器相同的float64 sigmoid及固定解码器。
        scores=1/(1+np.exp(-np.asarray(logits,float)))
        sources=task.decode_sources(d,scores,v)
        return dict(logits=logits,scores=scores,sources=sources)

    def infer_window(self,pcm,quiet_reference,*,sample_rate=39062.5,audio_start_s=None,audio_end_s=None):
        """One complete 1.048576s window, independent noise-only reference.

        A missing/dropped window is rejected upstream, never silently padded.
        No source type, speaker identity, true count or label parameter exists.
        """
        if float(sample_rate)!=39062.5:
            raise ValueError('audio rate must match frozen 39062.5Hz geometry')
        x=np.asarray(pcm);n=np.asarray(quiet_reference)
        if x.shape==(40960,16):x=x.reshape(20,2048,16)
        if n.shape==(8192,16):n=n.reshape(4,2048,16)
        if x.shape!=(20,2048,16) or n.shape!=(4,2048,16):
            raise ValueError('complete PCM[40960,16] and independent reference[8192,16] required')
        if not np.issubdtype(x.dtype,np.floating) or not np.issubdtype(n.dtype,np.floating):
            raise ValueError('explicitly decoded normalized floating PCM required; raw integer packets are not accepted')
        if not np.isfinite(x).all() or not np.isfinite(n).all() or np.max(np.abs(x))>1 or np.max(np.abs(n))>1:
            raise ValueError('finite normalized PCM/reference required')
        if np.shares_memory(x,n):
            raise ValueError('quiet reference cannot alias the tested audio window')
        if (audio_start_s is None)!=(audio_end_s is None):
            raise ValueError('both audio interval timestamps or neither required')
        if audio_start_s is not None:
            if not math.isfinite(audio_start_s) or not math.isfinite(audio_end_s):
                raise ValueError('finite interval timestamps required')
            if abs((audio_end_s-audio_start_s)-40960/39062.5)>1e-6:
                raise ValueError('timestamps do not describe a complete frozen-length audio window')
        start=time.perf_counter();observed=self.frontend.extract(x,n);front_end=time.perf_counter()-start
        tick=time.perf_counter();scored=self.score_observed_candidates(observed['features'],observed['directions'],observed['valid'])
        score_time=time.perf_counter()-tick
        # 投票观测是物理方向证据，不是标定SPL。
        return dict(contract_version=VERSION,checkpoint_sha256=CHECKPOINT_SHA,
            status='DEVELOPMENT_RESEARCH_PROTOTYPE_NOT_PROMOTED',sample_rate_hz=39062.5,window_samples=40960,
            audio_interval_s=None if audio_start_s is None else [float(audio_start_s),float(audio_end_s)],
            timestamp_domain='transport_audio_clock; video synchronization not implemented',
            threshold=.65,sources=scored['sources'],source_count=len(scored['sources']),
            ids_are_per_window_not_tracks=True,source_kind=None,parameters=28673,
            observed_directions=observed['directions'].tolist(),candidate_valid=observed['valid'].tolist(),
            candidate_logits=scored['logits'].tolist(),candidate_scores=scored['scores'].tolist(),
            physical_votes=observed['votes'].tolist(),physical_votes_are_SPL=False,
            timings_s=dict(physical_features=front_end,network_and_decode=score_time,total=front_end+score_time))
