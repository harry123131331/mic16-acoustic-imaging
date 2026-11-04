"""Frozen-shape, truth-free candidate features for a PC-only feasibility probe.

No network, output gate or train entry point lives here. Candidate directions
are already fixed from a complete window; conditional feature extraction is
causal in PCM with a common ten-sample DAS delay. This does not make the
window-level proposal generator causal or remove its 20-frame wait.
"""
from __future__ import annotations

import time
import numpy as np

from acoustic_camera.geometry_2d import expected_pair_lags_azel, far_field_arrival_delays_samples_azel
from acoustic_camera.valid_source_detection import angular_distance_deg
from mic16_config import Mic16Config, compact32_pairs

VERSION = "mic16_candidate_rescue_88_features_probe_v1_20261007"
FRAMES, CAP, FEATURES = 20, 16, 88
STAT_NAMES = ("raw_near_vote_fraction", "prefix_frame_support_fraction", "prefix_coarse_band_support_fraction",
              "mean_positive_peak_response", "mean_peak_contrast", "mean_angular_dispersion_fraction",
              "beam_energy_excess_div20", "beam_valid_sample_fraction")


def validate_directions(directions):
    d = np.asarray(directions, np.float64)
    if d.ndim != 2 or d.shape[1] != 2 or not 1 <= len(d) <= CAP or not np.isfinite(d).all():
        raise ValueError("directions must be finite [1..16,2]")
    # 有效边界网格点的球面重心可能略超出矩形
    # 网格范围，保留冻结结果，禁止裁剪。
    if np.any(np.abs(d) >= 90):
        raise ValueError("directions must lie in the front hemisphere")
    return d


def causal_candidate_beam(pcm, direction, cfg=Mic16Config()):
    """16ch interpolation DAS at original length, common causal delay + mask."""
    x = np.asarray(pcm, np.float32)
    if x.ndim == 3:
        x = x.reshape(-1, x.shape[-1])
    if x.ndim != 2 or x.shape[1] != cfg.channels or len(x) < 64 or not np.isfinite(x).all():
        raise ValueError("finite PCM must be [N>=64,16]")
    d = validate_directions(np.asarray(direction).reshape(1, 2))[0]
    arrivals = far_field_arrival_delays_samples_azel(*d, cfg).astype(np.float64)
    common_delay = int(np.ceil(cfg.array_radius_m*cfg.sample_rate/cfg.sound_speed))
    shifts = arrivals-common_delay
    if np.any(shifts > 0):
        raise ValueError("geometry exceeds common causal beam delay")
    axis = np.arange(len(x), dtype=np.float64)
    aligned = np.empty_like(x)
    for c in range(cfg.channels):
        aligned[:, c] = np.interp(axis+shifts[c], axis, x[:, c], left=0., right=0.)
    guard = int(np.ceil(-float(shifts.min())))+2
    valid = np.arange(len(x)) >= guard
    beam = aligned.mean(axis=1).astype(np.float32)
    beam[~valid] = 0.
    return beam, valid, dict(common_delay_samples=common_delay, left_guard_samples=guard,
                            common_delay_ms=1000*common_delay/cfg.sample_rate, no_future_pcm=True)


def spectral_observations(frames, reference, cfg=Mic16Config()):
    x, n = np.asarray(frames, float), np.asarray(reference, float)
    expected = (cfg.frame_samples, cfg.channels)
    if x.shape != (FRAMES, *expected) or n.ndim != 3 or n.shape[1:] != expected or len(n) < 1:
        raise ValueError("input must be [20,2048,16], independent reference [positive,2048,16]")
    if not np.isfinite(x).all() or not np.isfinite(n).all() or np.shares_memory(frames, reference):
        raise ValueError("finite, independent frames/reference required")
    frequency = np.fft.rfftfreq(cfg.fft_size, 1/cfg.sample_rate)
    bins = np.flatnonzero((frequency >= 300) & (frequency <= 8000))
    width = int(round(320/(cfg.sample_rate/cfg.fft_size)))
    zones = tuple(slice(i, min(i+width, len(bins))) for i in range(0, len(bins), width))
    window = np.hanning(cfg.frame_samples)
    def transform(values):
        centered = values-values.mean(axis=1, keepdims=True)
        return np.fft.rfft(centered*window[None, :, None], n=cfg.fft_size, axis=1)[:, bins]
    sx, sn = transform(x), transform(n)
    pairs = compact32_pairs(cfg)
    cross = sx[:, :, pairs[:, 0]]*sx[:, :, pairs[:, 1]].conj()
    cross = cross.transpose(0, 2, 1)
    magnitude = np.abs(cross)
    phat = np.divide(cross, magnitude, out=np.zeros_like(cross), where=magnitude > cfg.phat_epsilon).astype(np.complex64)
    px, pn = np.mean(np.abs(sx)**2, axis=2), np.mean(np.abs(sn)**2, axis=2)
    lx = np.stack([10*np.log10(np.maximum(px[:, z].mean(1), 1e-20)) for z in zones], 1)
    floor = np.array([np.median(10*np.log10(np.maximum(pn[:, z].mean(1), 1e-20))) for z in zones])
    return dict(phat=phat, excess_db=lx-floor, bins=bins, zones=zones, frequencies=frequency,
                allocated_retained_bytes=phat.nbytes+sx.nbytes+sn.nbytes+px.nbytes+pn.nbytes)


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


def beam_band_features(beam, mask, frame_count, cfg=Mic16Config()):
    values = np.asarray(beam, float).reshape(frame_count, cfg.frame_samples)
    valid = np.asarray(mask, bool).reshape(frame_count, cfg.frame_samples)
    centered = values-values.mean(axis=1, keepdims=True)
    power = np.abs(np.fft.rfft(centered*np.hanning(cfg.frame_samples), n=cfg.fft_size, axis=1))**2
    f = np.fft.rfftfreq(cfg.fft_size, 1/cfg.sample_rate)
    edges = np.linspace(300., 8000., 33)
    band = []
    for k in range(32):
        in_band = (f >= edges[k]) & (f < edges[k+1] if k < 31 else f <= edges[k+1])
        band.append(10*np.log10(np.maximum(power[:, in_band].mean(1), 1e-20)))
    level = 10*np.log10(np.maximum(np.mean(values**2, axis=1), 1e-20))
    return np.stack(band, 1), level, valid.mean(1)


def local_vote_statistics(votes, direction):
    a = np.asarray(votes, float)
    if a.shape != (480, 9) or not np.isfinite(a).all():
        raise ValueError("votes must be finite [480,9]")
    if set(a[:, 0]) != set(range(20)) or set(a[:, 1]) != set(range(24)) or not np.all(np.isin(a[:, 2], [0,1,2])):
        raise ValueError("invalid frame/zone/coarse-band indices")
    if len(set(map(tuple, a[:, :2]))) != 480:
        raise ValueError("duplicate frame-zone observations")
    errors = np.array([angular_distance_deg(*direction, *v[4:6]) for v in a])
    near = errors <= 10.
    result = np.zeros((FRAMES, 6), float)
    prefix_frames, prefix_bands = set(), set()
    for t in range(FRAMES):
        idx = np.flatnonzero(near & (a[:, 0] == t))
        if len(idx):
            prefix_frames.add(t); prefix_bands.update(a[idx, 2])
            result[t, 3] = np.maximum(a[idx, 7], 0).mean()
            result[t, 4] = np.clip(a[idx, 8], 0, 1).mean()
            result[t, 5] = errors[idx].mean()/10.
        result[t, 0] = len(idx)/24.
        result[t, 1] = len(prefix_frames)/(t+1)
        result[t, 2] = len(prefix_bands)/3.
    return result


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


def _minimum_cost_matching(pool, truths):
    errors = np.array([[angular_distance_deg(*p, *t) for t in truths] for p in pool]).reshape(len(pool), len(truths))
    states = {0:(0., [])}
    for p in range(len(pool)):
        for mask, (cost, pairs) in list(states.items()):
            for k in range(len(truths)):
                if mask & (1<<k) or errors[p, k] > 10:
                    continue
                new = mask | (1<<k); value = cost+float(errors[p, k])
                if new not in states or value < states[new][0]:
                    states[new] = (value, pairs+[(p, k, float(errors[p,k]))])
    best = max(states, key=lambda m:(m.bit_count(), -states[m][0]))
    return states[best][1], errors


def assign_window_labels(directions, speech_directions, nuisance_directions):
    """Metadata supervision ONLY, isolated from feature generation.

    One assigned positive per direct source; nearby unassigned/10..15deg
    candidates are ignored (-1), avoiding contradictory duplicate labels.
    Other directions are negatives, not certified reflections or VAD labels.
    """
    d = validate_directions(directions)
    speech = np.asarray(speech_directions, float).reshape(-1,2)
    nuisance = np.asarray(nuisance_directions, float).reshape(-1,2)
    if not np.isfinite(speech).all() or not np.isfinite(nuisance).all():
        raise ValueError("finite direction metadata required")
    truths = np.concatenate((speech, nuisance), 0)
    pairs, errors = _minimum_cost_matching(d, truths)
    geometry = np.zeros(len(d), np.int8); valid_speech = np.zeros(len(d), np.int8)
    for c in range(len(d)):
        if len(truths) and errors[c].min() <= 15:
            geometry[c] = -1
        if len(speech) and errors[c, :len(speech)].min() <= 15:
            valid_speech[c] = -1
    for c, k, _ in pairs:
        geometry[c] = 1
        if k < len(speech):
            valid_speech[c] = 1
    return dict(spatial=geometry.tolist(), valid_speech=valid_speech.tolist(),
                matching=[dict(candidate=c, source=k, speech=k<len(speech), error_deg=e) for c,k,e in pairs],
                label_kind="window metadata occupancy; not instantaneous VAD or measured direct-path dominance")
