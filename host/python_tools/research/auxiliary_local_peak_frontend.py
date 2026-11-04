"""Isolated same-capacity physical proposal probe; unchanged130 feature semantics."""
import sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/verification'))
from acoustic_camera.tf_zone_histogram_2d import TfZoneProjector
from acoustic_camera.tf_zone_neural_features import extract_neural_features
from research.auxiliary_local_peak_proposals import auxiliary_votes
from research.candidate_rescue_features import extract_candidate_features,causal_candidate_beam
from research.directional_tf_mask import observed_features
from research.multisource_split_network import detection_observations
from research.candidate_subband_competition import subband_contrasts
from prepare_candidate_rescue_windows import votes_from_maps
from audit_pre_gate_candidate_coverage import distances,clusters,fixed_bounded_pool


class AuxiliaryLocalPeakFrontend:
    def __init__(self):
        self.projector=TfZoneProjector()
        aa,ee=np.meshgrid(self.projector.grid.azimuths_deg,self.projector.grid.elevations_deg)
        self.grid=np.c_[aa.ravel(),ee.ravel()];self.allowed=distances(self.grid,self.grid)>=15.

    def extract(self,frames,reference):
        maps,_=extract_neural_features(self.projector,frames,reference)
        primary=votes_from_maps(self.projector,maps,frames,reference,self.allowed)
        augmented=auxiliary_votes(maps,primary,self.grid,self.allowed)
        all_votes=augmented['votes']
        pool=fixed_bounded_pool(clusters(all_votes,np.ones(len(all_votes),bool),10.))
        directions=np.asarray([c['direction'] for c in pool],float).reshape(-1,2)
        features=np.zeros((16,20,106),np.float32);padded=np.zeros((16,2),np.float32);valid=np.zeros(16,bool)
        count=len(directions);assert 1<=count<=16
        # 保留原480条投票统计语义，960条候选提议投票
        # 不得进入定义为480个独立时频观测的特征。
        physical,_=extract_candidate_features(frames,reference,directions,primary)
        beams=np.stack([causal_candidate_beam(frames,d)[0] for d in directions])
        spectral,_,_=observed_features(frames,directions,reference,beams)
        features[:count]=detection_observations(physical,spectral)
        padded[:count]=directions;valid[:count]=True
        contrast=subband_contrasts(features[None],padded[None],valid[None])[0]
        x=np.concatenate((features,contrast),-1)
        assert x.shape==(16,20,130) and np.isfinite(x).all()
        return dict(features=x,directions=padded,valid=valid,primary_votes=primary,
            augmented_votes=all_votes,auxiliary_votes=augmented['auxiliary_votes'],maps=maps,
            diagnostics=augmented['diagnostics'])
