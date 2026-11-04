"""One-factor observed-only first-zone extension, not a promoted detector.

Old 24 frequency zones are retained. Only the first zone additionally contains
200 <= f < the old minimum bin. Other zone frequency memberships are identical.
"""
import numpy as np
from acoustic_camera.tf_zone_histogram_2d import TfZoneProjector,TfZoneAnalysisConfig
from research.meeting_room_detection_features import MeetingRoomFeatures

VERSION='first_TF_zone_lower_edge_200Hz_same24_zones_20261013'
LOW_HZ=200.

def build_projector():
    original=TfZoneProjector()
    expanded=TfZoneProjector(cfg=original.cfg,grid=original.grid,
        config=TfZoneAnalysisConfig(frequency_min_hz=LOW_HZ,
            frequency_max_hz=original.config.frequency_max_hz,
            zone_width_hz=original.config.zone_width_hz,
            spatial_second_peak_nms_deg=original.config.spatial_second_peak_nms_deg))
    extra=int(np.searchsorted(expanded.frequency_bins,original.frequency_bins[0]))
    assert extra>0 and np.array_equal(expanded.frequency_bins[extra:],original.frequency_bins)
    expanded.zone_slices=(slice(0,extra+original.zone_slices[0].stop),)+tuple(
        slice(z.start+extra,z.stop+extra) for z in original.zone_slices[1:])
    assert len(expanded.zone_slices)==len(original.zone_slices)==24
    assert np.array_equal(expanded.signatures[:,:,extra:],original.signatures)
    for old,new in zip(original.zone_slices[1:],expanded.zone_slices[1:]):
        assert np.array_equal(original.frequency_bins[old],expanded.frequency_bins[new])
    f=expanded.frequencies_hz
    info=dict(version=VERSION,requested_lower_edge_hz=LOW_HZ,new_bins=extra,zone_count=24,
        original_first_zone_hz=[float(original.frequencies_hz[original.zone_slices[0]][0]),
                               float(original.frequencies_hz[original.zone_slices[0]][-1])],
        expanded_first_zone_hz=[float(f[expanded.zone_slices[0]][0]),float(f[expanded.zone_slices[0]][-1])],
        added_frequencies_hz=f[:extra].tolist(),high23_zone_memberships_and_signatures_identical=True,
        fft_size=original.cfg.fft_size,frame_samples=original.cfg.frame_samples,
        feature_physical88_and_spectral18_extractors_unchanged=True,
        proposal_capacity=16,neural_feature_width=106)
    return original,expanded,info

class FirstZoneLowProposalFeatures(MeetingRoomFeatures):
    """Existing 106-wide extraction, with only proposal/vote first-zone change.

    Physical88 PHAT/beam feature bands stay original300..8000Hz; supplemental
    spectral18 extractor stays unchanged. No whole-frontend lowcut change.
    """
    def __init__(self):
        super().__init__()
        _,self.projector,self.extension_info=build_projector()
