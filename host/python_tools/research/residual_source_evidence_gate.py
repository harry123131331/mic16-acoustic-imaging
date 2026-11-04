"""Observed-only physical evidence gate for an isolated residual pilot.

Reuse frozen physical3dB/.15/.02 values and existing neural.65; the empirical
quiet maximum is an envelope, not a calibrated false-alarm probability.
No source class, truth position, known count or residual normalization.
"""
import numpy as np
from acoustic_camera.geometry_2d import expected_pair_lags_azel
from acoustic_camera.tf_zone_histogram_2d import TfZoneThresholds
from research.incremental_source_search import points
from research.incremental_masked_frontend import rescan_votes
from research.known_direction_phase_residual import project_known_components
from research.candidate_rescue_features import extract_candidate_features,causal_candidate_beam
from research.directional_tf_mask import observed_features
from research.multisource_split_network import detection_observations
from research.candidate_subband_competition import subband_contrasts
from audit_pre_gate_candidate_coverage import clusters,fixed_bounded_pool
VERSION='known_phase_residual_evidence_gate_fixed_threshold_pilot_20261015'

def phase_observations(projector,frames):
    return np.stack([projector._observation(projector._frame_spectra(f)) for f in frames])

def response_maps(projector,observations):
    maps=[]
    for z in projector.zone_slices:
        signature=projector.signatures[:,:,z]
        value=np.real(np.einsum('gpf,tpf->tg',signature.conj(),observations[:,:,z],optimize=True))
        maps.append((value/(signature.shape[1]*signature.shape[2])).reshape(len(observations),*projector.grid.shape))
    return np.stack(maps,axis=1).astype(np.float32)

def known_phase_maps(projector,observations,quiet_observations,original_maps,original_quiet_maps,known_directions):
    directions=points(known_directions)
    if not len(directions):return original_maps.copy(),original_quiet_maps.copy(),dict(operator_used=False,ranks=[0]*24)
    lag=np.stack([expected_pair_lags_azel(*d,projector.cfg) for d in directions]).astype(float)
    signatures=np.exp(1j*2*np.pi*lag[:,:,None]*projector.frequency_bins[None,None,:]/projector.cfg.fft_size).astype(np.complex64)
    residual=observations.astype(np.complex128).copy();quiet=quiet_observations.astype(np.complex128).copy();ranks=[];errors=[]
    for zone in projector.zone_slices:
        atoms=signatures[:,:,zone].reshape(len(directions),-1)
        projected,qa=project_known_components(residual[:,:,zone].reshape(len(residual),-1),atoms)
        qprojected,qqa=project_known_components(quiet[:,:,zone].reshape(len(quiet),-1),atoms)
        residual[:,:,zone]=projected.reshape(len(residual),32,-1);quiet[:,:,zone]=qprojected.reshape(len(quiet),32,-1)
        ranks.append(qa['operator_rank']);errors.extend((qa['maximum_residual_known_component'],qqa['maximum_residual_known_component']))
    return response_maps(projector,residual),response_maps(projector,quiet),dict(operator_used=True,ranks=ranks,
        maximum_residual_known_component=float(max(errors)),normalized_after_projection=False)

def gated_votes(maps,quiet_maps,primary,grid,allowed_separation,search_mask):
    thresholds=TfZoneThresholds();mask=np.asarray(search_mask,bool)
    if quiet_maps.shape!=(4,24,13,25):raise ValueError('four independent quiet frames required')
    votes=rescan_votes(maps,primary,grid,allowed_separation,mask)
    if votes is None:return None,np.zeros(480,bool),dict(status='NO_SEARCH_GRID',eligible_votes=0)
    reference=np.max(quiet_maps.reshape(4,24,-1)[:,:,mask],axis=(0,2))
    zi=votes[:,1].astype(int)
    tests=dict(energy=votes[:,6]>=thresholds.minimum_energy_excess_db,
        coherence=votes[:,7]>=thresholds.minimum_spatial_coherence,
        reference_envelope=votes[:,7]>reference[zi]+1e-8,
        contrast=votes[:,8]>=thresholds.minimum_peak_contrast)
    eligible=np.logical_and.reduce(list(tests.values()))
    return votes,eligible,dict(status='EVIDENCE_VOTES' if eligible.any() else 'STOP_NO_PHYSICAL_EVIDENCE',
        eligible_votes=int(eligible.sum()),reference_peak_by_zone=reference.tolist(),
        independent_rejection_counts={name:int(np.sum(~check)) for name,check in tests.items()},
        thresholds=dict(energy_excess_db=3.,coherence=.15,contrast=.02,neural=.65),
        empirical_reference_envelope_not_false_alarm_probability=True)

def evidence_candidate_features(frames,reference,original_votes,search_votes,eligible):
    # 新候选使用合格投票，保留网络在这些方向
    # 仍读取原始观测定义和原投票统计。
    x=np.zeros((16,20,106),np.float32);d=np.zeros((16,2),np.float32);valid=np.zeros(16,bool)
    pool=fixed_bounded_pool(clusters(search_votes,np.asarray(eligible,bool),10.)) if search_votes is not None else []
    if pool:
        directions=np.asarray([p['direction'] for p in pool],float);count=len(pool)
        physical,_=extract_candidate_features(frames,reference,directions,original_votes)
        beams=np.stack([causal_candidate_beam(frames,p)[0] for p in directions])
        spectral,_,_=observed_features(frames,directions,reference,beams)
        x[:count]=detection_observations(physical,spectral);d[:count]=directions;valid[:count]=True
    contrast=subband_contrasts(x[None],d[None],valid[None])[0]
    return dict(features=np.concatenate((x,contrast),-1),directions=d,valid=valid,
        pool=pool,features_use_original_audio_and_votes=True)
