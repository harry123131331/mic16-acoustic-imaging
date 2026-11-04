"""Observed-only front end shared by new-room preflight and future trainer."""
import sys
from pathlib import Path
import numpy as np
from research.candidate_rescue_features import extract_candidate_features,causal_candidate_beam
from research.directional_tf_mask import observed_features
from research.multisource_split_network import detection_observations
from acoustic_camera.tf_zone_histogram_2d import TfZoneProjector
from acoustic_camera.tf_zone_neural_features import extract_neural_features

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/verification'))
from audit_pre_gate_candidate_coverage import clusters,fixed_bounded_pool,distances
from prepare_candidate_rescue_windows import votes_from_maps


class MeetingRoomFeatures:
    def __init__(self):
        self.projector=TfZoneProjector()
        aa,ee=np.meshgrid(self.projector.grid.azimuths_deg,self.projector.grid.elevations_deg)
        points=np.c_[aa.ravel(),ee.ravel()]
        self.allowed=distances(points,points)>=15

    def extract(self,frames,reference):
        p=self.projector
        maps,_=extract_neural_features(p,frames,reference)
        votes=votes_from_maps(p,maps,frames,reference,self.allowed)
        pool=fixed_bounded_pool(clusters(votes,np.ones(480,bool),10.))
        directions=np.asarray([c['direction'] for c in pool]);count=len(directions)
        physical,_=extract_candidate_features(frames,reference,directions,votes)
        beams=np.stack([causal_candidate_beam(frames,d)[0] for d in directions])
        spectral,_,_=observed_features(frames,directions,reference,beams)
        x=detection_observations(physical,spectral)
        features=np.zeros((16,20,106),np.float32);features[:count]=x
        padded=np.zeros((16,2),np.float32);padded[:count]=directions
        valid=np.zeros(16,bool);valid[:count]=True
        assert np.isfinite(features).all() and len(votes)==480
        return dict(features=features,directions=padded,valid=valid,votes=votes)
