"""RAM-only SBL proposal frontend, using the unchanged 130 scoring inputs."""
import numpy as np
from acoustic_camera.tf_zone_histogram_2d import TfZoneProjector
from acoustic_camera.tf_zone_neural_features import extract_neural_features
from research.spatial_sbl_proposals import SpatialSBLProposals
from research.candidate_rescue_features import extract_candidate_features,causal_candidate_beam
from research.directional_tf_mask import observed_features
from research.multisource_split_network import detection_observations
from research.candidate_subband_competition import subband_contrasts
from prepare_candidate_rescue_windows import votes_from_maps
from audit_pre_gate_candidate_coverage import distances


def project_quiet_maps(projector,reference):
    """Same phase projector without a second floor/quality computation.

    The independent quiet reference supplies measured spatial variance. It is
    not reinterpreted as a foreground window, and no signal-dependent spectral
    floor is used to alter its spatial maps.
    """
    spectrum=np.stack([projector._frame_spectra(f) for f in reference])
    observations=np.stack([projector._observation(f) for f in spectrum])
    out=[]
    for zone in projector.zone_slices:
        signature=projector.signatures[:,:,zone]
        value=np.real(np.einsum('gpf,tpf->tg',signature.conj(),observations[:,:,zone],optimize=True))
        out.append((value/(signature.shape[1]*signature.shape[2])).reshape(len(reference),13,25))
    return np.stack(out,axis=1).astype(np.float32)


class SpatialSBLFrontend:
    def __init__(self):
        self.projector=TfZoneProjector();self.proposal=SpatialSBLProposals(self.projector)
        self.allowed=distances(self.proposal.directions,self.proposal.directions)>=15.

    def extract(self,frames,reference):
        p=self.projector
        maps,_=extract_neural_features(p,frames,reference)
        quiet_maps=project_quiet_maps(p,reference)
        votes=votes_from_maps(p,maps,frames,reference,self.allowed)
        result=self.proposal.propose(maps,quiet_maps)
        directions=np.asarray([r['direction'] for r in result['pool']],np.float64).reshape(-1,2)
        count=len(directions);features=np.zeros((16,20,106),np.float32)
        padded=np.zeros((16,2),np.float32);valid=np.zeros(16,bool)
        if count:
            physical,_=extract_candidate_features(frames,reference,directions,votes)
            beams=np.stack([causal_candidate_beam(frames,d)[0] for d in directions])
            spectral,_,_=observed_features(frames,directions,reference,beams)
            features[:count]=detection_observations(physical,spectral)
            padded[:count]=directions;valid[:count]=True
        contrast=subband_contrasts(features[None],padded[None],valid[None])[0]
        x=np.concatenate((features,contrast),axis=-1)
        assert x.shape==(16,20,130) and np.isfinite(x).all() and len(votes)==480
        return dict(features=x,directions=padded,valid=valid,votes=votes,maps=maps,
            gamma=result['gamma'],diagnostics=result['diagnostics'])
