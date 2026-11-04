"""Separate C16/C32 capacity control; original baseline remains immutable.



Frozen feature equations copied verbatim except capacity guards. No labels,

source class/count/identity or true directions enter the observation pipeline.

The C32 context and spectral other-beam maximum intentionally change: old

scores cannot be presumed preserved. No trained C32 checkpoint exists yet.

"""

import math

import time

import sys

from pathlib import Path

import numpy as np

import torch

from torch.nn import functional as F

from acoustic_camera.geometry_2d import expected_pair_lags_azel

from acoustic_camera.valid_source_detection import angular_distance_deg

from mic16_config import Mic16Config, compact32_pairs

from research.candidate_rescue_features import (spectral_observations,

    causal_candidate_beam, beam_band_features, local_vote_statistics)

from research.directional_tf_mask import stft, resample, band_mean

from research.multisource_split_network import (DetectionNetwork as OriginalDetectionNetwork,

    detection_observations, DET_INPUT, DET_HIDDEN)

from research.candidate_joint_validity_network import observed_temporal_statistics

from research.all_source_spatial_capacity import spatial_candidates

from research.meeting_room_detection_features import MeetingRoomFeatures



ROOT = Path(__file__).resolve().parents[2]

sys.path.insert(0, str(ROOT/'tools/verification'))

from audit_pre_gate_candidate_coverage import clusters

from prepare_candidate_rescue_windows import votes_from_maps

from acoustic_camera.tf_zone_neural_features import extract_neural_features



VERSION = 'mic16_meeting_room_preserved_prefix_capacity_control_20261012'

CAP, FRAMES, FEATURES = 32, 20, 88

RATE, FFT, HOP, BANDS, INPUTS = 16000, 512, 256, 64, 10

RADIUS_DEG = 15.0







def validate_directions(directions):

    d = np.asarray(directions, np.float64)

    if d.ndim != 2 or d.shape[1] != 2 or not 1 <= len(d) <= CAP or not np.isfinite(d).all():

        raise ValueError("directions must be finite [1..32,2]")

    # 有效边界网格点的球面重心可能略超出矩形

    # 网格范围，保留冻结结果，禁止裁剪。

    if np.any(np.abs(d) >= 90):

        raise ValueError("directions must lie in the front hemisphere")

    return d





def candidate_responses(observation, directions, cfg=Mic16Config()):

    d = validate_directions(directions)

    bins = observation["bins"]

    lags = np.stack([expected_pair_lags_azel(*direction, cfg, clip=True) for direction in d]).astype(float)

    signature = np.exp(1j*2*np.pi*lags[:, :, None]*bins[None, None, :]/cfg.fft_size).astype(np.complex64)

    maps = []

    for z in observation["zones"]:

        score = np.real(np.einsum("cpf,tpf->ct", signature[:, :, z].conj(), observation["phat"][:, :, z], optimize=True))

        score /= signature.shape[1]*len(bins[z])

        maps.append(score)

    return np.stack(maps, 2).astype(np.float32), signature.nbytes





def extract_candidate_features(frames, reference, directions, votes, cfg=Mic16Config()):

    """No truth/role/label argument; outputs unstandardized fixed-scale features."""

    d = validate_directions(directions)

    start = time.perf_counter()

    observation = spectral_observations(frames, reference, cfg)

    after_spectrum = time.perf_counter()

    response, signature_bytes = candidate_responses(observation, d, cfg)

    after_response = time.perf_counter()

    features = np.zeros((len(d), FRAMES, FEATURES), np.float32)

    diagnostics = []

    for c, direction in enumerate(d):

        beam, valid, info = causal_candidate_beam(frames, direction, cfg)

        band, level, valid_fraction = beam_band_features(beam, valid, FRAMES, cfg)

        quiet_beam, quiet_valid, _ = causal_candidate_beam(reference, direction, cfg)

        _, quiet_level, _ = beam_band_features(quiet_beam, quiet_valid, len(reference), cfg)

        local = local_vote_statistics(votes, direction)

        features[c, :, :24] = response[c]

        features[c, :, 24:48] = np.clip(observation["excess_db"], -20, 40)/20

        features[c, :, 48:80] = np.clip(band, -100, 80)/40

        features[c, :, 80:86] = local

        features[c, :, 86] = np.clip(level-np.median(quiet_level), -20, 40)/20

        features[c, :, 87] = valid_fraction

        diagnostics.append(dict(direction=direction.tolist(), beam=info,

                                median_energy_excess_db=float(np.median(level-np.median(quiet_level)))))

    after_features = time.perf_counter()

    if not np.isfinite(features).all():

        raise RuntimeError("non-finite candidate features")

    return features, dict(version=VERSION, directions=d.tolist(), candidates=diagnostics,

                          input_truth_free=True, logical_channel_order="identity, same as sealed synthetic PCM",

                          timings_ms=dict(spectra_and_phat=1000*(after_spectrum-start), candidate_responses=1000*(after_response-after_spectrum),

                                          beams_bands_vote_stats=1000*(after_features-after_response), total=1000*(after_features-start)),

                          retained_spectral_arrays_bytes=observation["allocated_retained_bytes"],

                          candidate_signature_bytes=signature_bytes,

                          tensor_bytes=features.nbytes)





def observed_features(frames, directions, reference, beam_waveforms):

    """No components, activity, labels, source count or identity arguments."""

    cfg = Mic16Config()

    d = np.asarray(directions, float)

    pcm = np.asarray(frames, float).reshape(-1, 16)

    quiet = np.asarray(reference, float).reshape(-1, 16)

    beams = np.asarray(beam_waveforms, float)

    if d.shape != (len(beams), 2) or not 1 <= len(d) <= CAP or beams.shape[1:] != (len(pcm),):

        raise ValueError("matching directions[C,2], beams[C,N], 1..32 candidates required")

    if not np.isfinite(d).all() or np.any(np.abs(d) >= 90):

        raise ValueError("finite front-hemisphere estimated directions required")

    observed = stft(resample(pcm))

    quiet_spectrum = stft(resample(quiet))

    beam_spectra = np.stack([stft(resample(b)) for b in beams])

    power = np.stack([band_mean(np.abs(b)**2) for b in beam_spectra])

    channel_power = band_mean((np.abs(observed)**2).mean(axis=2))

    quiet_power = np.median(band_mean((np.abs(quiet_spectrum)**2).mean(axis=2)), axis=0)

    pairs = compact32_pairs(cfg)

    cross = observed[:, :, pairs[:, 0]]*observed[:, :, pairs[:, 1]].conj()

    magnitude = np.abs(cross)

    phat = np.divide(cross, magnitude, out=np.zeros_like(cross), where=magnitude > cfg.phat_epsilon)

    frequencies = np.fft.rfftfreq(FFT, 1/RATE)

    features = np.zeros((len(d), len(observed), BANDS, INPUTS), np.float32)

    for c, direction in enumerate(d):

        lags = expected_pair_lags_azel(*direction, cfg, clip=False).astype(float)/cfg.sample_rate

        signature = np.exp(1j*2*np.pi*frequencies[:, None]*lags[None, :])

        compensated = phat*signature[None, :, :].conj()

        short = band_mean(compensated[:, :, :16].mean(axis=2))

        long = band_mean(compensated[:, :, 16:].mean(axis=2))

        plane = band_mean(np.abs(compensated.mean(axis=2)))

        features[c, :, :, :5] = np.stack((short.real, short.imag, long.real, long.imag, plane), axis=-1)

        other = np.max(np.delete(power, c, axis=0), axis=0) if len(d) > 1 else power[c]

        features[c, :, :, 5] = np.clip(10*np.log10(np.maximum(power[c], 1e-20)), -100, 80)/40

        for k, denominator, low, high in ((6, channel_power, -40, 20), (7, other, -40, 20), (8, quiet_power, -20, 40)):

            ratio = 10*np.log10(np.maximum(power[c], 1e-20)/np.maximum(denominator, 1e-20))

            features[c, :, :, k] = np.clip(ratio, low, high)/20

        features[c, :, :, 9] = frequencies[1:].reshape(BANDS, 4).mean(1)[None, :]/8000

    weight = power/np.maximum(power.mean(axis=(1, 2), keepdims=True), 1e-20)

    assert np.isfinite(features).all() and np.isfinite(weight).all()

    return features, weight.astype(np.float32), beam_spectra





def neighborhood_mask(directions, valid):

    """Spherical neighborhood, excluding self and padding; no true DOA/K."""

    if directions.ndim != 3 or directions.shape[-1] != 2 or valid.shape != directions.shape[:2]:

        raise ValueError("observed directions[B,C,2], valid[B,C] required")

    if not 1 <= directions.shape[1] <= CAP or not torch.isfinite(directions).all():

        raise ValueError("finite observed directions and 1..32 candidates required")

    radians = directions * (math.pi / 180)

    azimuth, elevation = radians.unbind(dim=-1)

    unit = torch.stack((elevation.cos()*azimuth.sin(), elevation.sin(),

                        elevation.cos()*azimuth.cos()), dim=-1)

    dot = unit @ unit.transpose(-1, -2)

    mask = dot >= math.cos(math.radians(RADIUS_DEG))

    mask &= valid.bool().unsqueeze(-1) & valid.bool().unsqueeze(-2)

    mask &= ~torch.eye(directions.shape[1], dtype=torch.bool, device=directions.device).unsqueeze(0)

    return mask





def decode_joint_validity(directions, probabilities, candidate_valid, threshold):

    """Decode every candidate; there is no permanently accepted physical list.



    Score descending, original-index ties, 10-degree duplicate exclusion and

    at most eight outputs. Scores are uncalibrated, not correctness probabilities.

    """

    d, p, valid = np.asarray(directions, float), np.asarray(probabilities, float), np.asarray(candidate_valid, bool)

    if d.ndim != 2 or d.shape[1] != 2 or p.shape != (len(d),) or valid.shape != p.shape or not 1 <= len(d) <= CAP:

        raise ValueError("directions[1..32,2], probabilities[C], valid[C] required")

    if not np.isfinite(d).all() or not np.isfinite(p).all() or not np.isfinite(threshold) or not 0 <= threshold <= 1 or np.any((p < 0) | (p > 1)):

        raise ValueError("finite directions and probabilities/threshold in [0,1] required")

    outputs, selected = [], []

    for i in sorted((j for j in range(len(d)) if valid[j] and p[j] >= threshold), key=lambda j: (-p[j], j)):

        if len(outputs) == 8:

            break

        if any(angular_distance_deg(*d[i], *point) <= 10 for point in outputs):

            continue

        outputs.append(d[i].tolist())

        selected.append(i)

    return outputs, selected





class CapacityDetectionNetwork(OriginalDetectionNetwork):

    """Identical learned layers; count/(capacity-1) retains occupancy meaning."""

    def __init__(self, capacity=32):

        if capacity not in (16, 32):

            raise ValueError('only registered C16/C32 controls allowed')

        super().__init__()

        self.capacity = capacity



    def forward(self, features, directions, valid):

        b, c, t, f = features.shape

        if not 1 <= c <= self.capacity or not torch.isfinite(features).all():

            raise ValueError('finite features within registered capacity required')

        assert (t, f) == (20, DET_INPUT) and valid.shape == (b, c)

        h = F.relu(self.project(features)).reshape(b*c, t, DET_HIDDEN).transpose(1, 2)

        for layer, dilation in zip(self.temporal, (1, 2, 4)):

            h = F.relu(h + layer(F.pad(h, (2*dilation, 0))))

        h = h.reshape(b, c, DET_HIDDEN, t)

        own = torch.cat((h.mean(-1), h.amax(-1), observed_temporal_statistics(features[..., :88])), dim=-1)

        neighbors = neighborhood_mask(directions, valid)

        count = neighbors.sum(-1, keepdim=True); exists = count > 0

        mean = neighbors.to(own.dtype) @ own / count.clamp_min(1)

        maximum = own.unsqueeze(1).expand(-1, c, -1, -1).masked_fill(~neighbors.unsqueeze(-1), -torch.inf).amax(2)

        maximum = torch.where(exists, maximum, torch.zeros_like(maximum))

        context = torch.cat((own, torch.where(exists, own-mean, torch.zeros_like(own)),

            torch.where(exists, own-maximum, torch.zeros_like(own)), count.to(own.dtype)/(self.capacity-1), exists.to(own.dtype)), dim=-1)

        return self.head(context).squeeze(-1).masked_fill(~valid, -20.)





class CapacityMeetingRoomFeatures(MeetingRoomFeatures):

    def __init__(self, capacity=32):

        if capacity not in (16, 32):

            raise ValueError('only registered C16/C32 controls allowed')

        super().__init__()

        self.capacity = capacity



    def extract(self, frames, reference):

        p = self.projector

        maps, _ = extract_neural_features(p, frames, reference)

        votes = votes_from_maps(p, maps, frames, reference, self.allowed)

        pool = spatial_candidates(clusters(votes, np.ones(480, bool), 10.), self.capacity)

        directions = np.asarray([c['direction'] for c in pool]); count = len(directions)

        physical, _ = extract_candidate_features(frames, reference, directions, votes)

        beams = np.stack([causal_candidate_beam(frames, d)[0] for d in directions])

        spectral, _, _ = observed_features(frames, directions, reference, beams)

        x = detection_observations(physical, spectral)

        features = np.zeros((self.capacity, 20, 106), np.float32); features[:count] = x

        padded = np.zeros((self.capacity, 2), np.float32); padded[:count] = directions

        valid = np.zeros(self.capacity, bool); valid[:count] = True

        assert np.isfinite(features).all() and len(votes) == 480

        return dict(features=features, directions=padded, valid=valid, votes=votes)





def capacity_budget(capacity):

    if capacity not in (16, 32):

        raise ValueError('only registered C16/C32 controls allowed')

    return dict(capacity=capacity, parameters=28673, fp32_weight_bytes=114692,

        dense_MAC_per_window=capacity*(20*(106*32+3*3*32*32)+(215*64+64*32+32)),

        padded_fp32_feature_bytes=capacity*20*106*4,

        candidate_pair_slots=capacity*capacity, max_directed_neighbor_edges=capacity*(capacity-1),

        excludes='FFT/PHAT/DAS/STFT/reductions/activations/IO; not total peak RAM or measured FPGA cost')



