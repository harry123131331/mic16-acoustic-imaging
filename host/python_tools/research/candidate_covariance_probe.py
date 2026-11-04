"""Observed-only joint spatial covariance fit; diagnostic, not a detector.

The multi-plane-wave PSD model follows Braun & Habets (2015), Eq. 3/12.
Our nonnegative ridge fit and pooling are adaptations, not that paper's MLE.
Coherent early reflections violate the uncorrelated-component assumption.
No true K, source kind, room parameter, source component or label is accepted.
"""
from __future__ import annotations

import time
import numpy as np
from scipy.linalg import solve_triangular
from scipy.optimize import nnls
from acoustic_camera.geometry_2d import far_field_arrival_delays_samples_azel, mic_positions_xyz
from acoustic_camera.valid_source_detection import angular_distance_deg
from mic16_config import Mic16Config

VERSION = "observed_joint_covariance_TRAIN_probe_fft4096_repair_20261013"
RIDGE = 1e-5
PAST_FRAMES = 5
TOP_FRACTION = .1


def dictionary(directions, frequencies, cfg=Mic16Config()):
    directions = np.asarray(directions, float)
    assert directions.ndim == 2 and directions.shape[1] == 2
    assert 1 <= len(directions) <= 16 and np.isfinite(directions).all()
    delays = np.stack([far_field_arrival_delays_samples_azel(*d, cfg) for d in directions]) / cfg.sample_rate
    a = np.exp(-2j*np.pi*np.asarray(frequencies)[:, None, None]*delays[None])
    m = cfg.channels
    spatial = a[..., :, None]*a[..., None, :].conj()/m
    pos = mic_positions_xyz(cfg).astype(float)
    dist = np.linalg.norm(pos[:, None]-pos[None, :], axis=-1)
    # NumPy的sinc使用πx，论文公式5使用sin(kd)/(kd)。
    diffuse = np.sinc(2*np.asarray(frequencies)[:, None, None]*dist[None]/cfg.sound_speed)/m
    white = np.broadcast_to(np.eye(m)/m, diffuse.shape)
    return np.concatenate((spatial, diffuse[:, None], white[:, None]), axis=1)


def fit_covariance(covariance, basis):
    """Exact convex NNLS via Gram Cholesky; all coefficients are inferred.

    Minimize ||R-sum_j p_j D_j||_F^2 + RIDGE*||p||^2, p>=0.
    There is no neural-network training, gradient or parameter update here.
    """
    gram = np.einsum('fjab,fkab->fjk', basis.conj(), basis).real
    gram += RIDGE*np.eye(gram.shape[-1])[None]
    rhs = np.einsum('fjab,tfab->tfj', basis.conj(), covariance).real
    factor = np.linalg.cholesky(gram)
    values = np.empty_like(rhs)
    error = 0.
    for f in range(len(basis)):
        z = solve_triangular(factor[f], rhs[:, f].T, lower=True).T
        for t in range(len(covariance)):
            values[t, f] = nnls(factor[f].T, z[t], maxiter=10*gram.shape[-1])[0]
    gradient = np.einsum('fjk,tfk->tfj', gram, values)-rhs
    error = max(float(np.max(np.maximum(-gradient, 0))),
                float(np.max(np.abs(values*gradient))))
    return values, dict(max_KKT_violation=error, solves=int(len(covariance)*len(basis)))


def explicit_check(covariance, basis, coefficients, samples):
    """Independent explicit real/imaginary least squares versus Gram form."""
    max_coefficient_error = 0.
    for t, f in samples:
        rows = basis[f].reshape(len(basis[f]), -1).T
        design = np.concatenate((rows.real, rows.imag), axis=0)
        design = np.vstack((design, np.sqrt(RIDGE)*np.eye(len(basis[f]))))
        y = covariance[t, f].reshape(-1)
        y = np.r_[y.real, y.imag, np.zeros(len(basis[f]))]
        independent = nnls(design, y, maxiter=10*design.shape[1])[0]
        max_coefficient_error = max(max_coefficient_error, float(np.max(np.abs(independent-coefficients[t, f]))))
    return max_coefficient_error


def extract(frames, reference, directions, cfg=Mic16Config()):
    start = time.perf_counter()
    x, n = np.asarray(frames, float), np.asarray(reference, float)
    assert x.shape == (20, 2048, 16) and n.shape == (4, 2048, 16)
    assert np.isfinite(x).all() and np.isfinite(n).all()
    assert not np.shares_memory(frames, reference)
    f = np.fft.rfftfreq(cfg.fft_size, 1/cfg.sample_rate)
    bins = np.flatnonzero((f >= 300) & (f <= 8000))
    win = np.hanning(cfg.frame_samples)
    def transform(y):
        spectrum = np.fft.rfft((y-y.mean(1, keepdims=True))*win[None, :, None], n=cfg.fft_size, axis=1)
        assert spectrum.shape[1] == len(f), 'FFT length must match frequency-axis length'
        return spectrum[:, bins]
    sx, sn = transform(x), transform(n)
    # 独立冻结前端核对频点、相位符号及加窗。
    from research.candidate_rescue_features import spectral_observations
    from mic16_config import compact32_pairs
    original = spectral_observations(frames, reference, cfg)
    assert np.array_equal(original['bins'], bins)
    pairs = compact32_pairs(cfg)
    cross = (sx[:, :, pairs[:, 0]]*sx[:, :, pairs[:, 1]].conj()).transpose(0, 2, 1)
    magnitude = np.abs(cross)
    phat = np.divide(cross, magnitude, out=np.zeros_like(cross), where=magnitude > cfg.phat_epsilon).astype(np.complex64)
    spectral_error = float(np.max(np.abs(phat-original['phat'])))
    assert spectral_error < 2e-7
    del original, cross, magnitude, phat
    instant = sx[..., :, None]*sx[..., None, :].conj()
    cov = np.stack([instant[max(0, t-PAST_FRAMES+1):t+1].mean(0) for t in range(20)])
    trace = np.trace(cov, axis1=-2, axis2=-1).real
    quiet_trace = np.mean(np.sum(np.abs(sn)**2, axis=-1), axis=0)
    normalized = cov/np.maximum(trace[..., None, None], 1e-30)
    basis = dictionary(directions, f[bins], cfg)
    coefficients, numerical = fit_covariance(normalized, basis)
    check_indices = [(0, 0), (4, len(bins)//4), (10, len(bins)//2), (19, len(bins)-1)]
    independent = explicit_check(normalized, basis, coefficients, check_indices)
    assert numerical['max_KKT_violation'] < 1e-8 and independent < 2e-7
    # 观测超额能量采用软值，不是活动门控、SPL或标定信噪比。
    excess = np.clip(1-quiet_trace[None]/np.maximum(trace, 1e-30), 0, 1)
    neighborhood = np.array([[angular_distance_deg(*a, *b) <= 10 for b in directions] for a in directions])
    share = np.einsum('cd,tfd->ctf', neighborhood.astype(float), coefficients[..., :len(directions)])
    share /= np.maximum(coefficients.sum(-1)[None], 1e-20)
    cue = np.clip(share, 0, 1)*excess[None]
    # 时间/频率上固定取前10%，不按真值或开发校准集调节。
    flat = cue.reshape(len(directions), -1)
    count = int(np.ceil(flat.shape[1]*TOP_FRACTION))
    summary = np.sort(flat, axis=1)[:, -count:].mean(1)
    return dict(cue=summary, frame_mean=cue.mean(-1), cue_tf=cue, bins=bins,
        frequencies=f[bins], numerical=dict(**numerical, explicit_checks=4,
        explicit_max_coefficient_error=independent,FFT_length=cfg.fft_size,
        frozen_frontend_PHAT_max_error=spectral_error), duration_s=time.perf_counter()-start,
        covariance_shape=list(cov.shape), coefficient_shape=list(coefficients.shape),
        principal_arrays_bytes=sum(a.nbytes for a in (sx, sn, instant, cov, normalized, basis, coefficients, cue)))


def analytic_checks():
    directions = np.array([[-45., -15.], [30., 20.]])
    basis = dictionary(directions, np.array([1500., 4000.]))
    known = np.array([[[.35, .45, .15, .05], [.35, .45, .15, .05]]])
    covariance = np.einsum('tfj,fjab->tfab', known, basis)
    actual, info = fit_covariance(covariance, basis)
    # 岭正则故意非零，因此比较重构协方差。
    reconstructed = np.einsum('tfj,fjab->tfab', actual, basis)
    error = float(np.max(np.abs(reconstructed-covariance)))
    independent = explicit_check(covariance, basis, actual, [(0, 0), (0, 1)])
    assert error < 3e-5 and independent < 2e-7
    # 通过既有独立通道对延迟合同检查延迟符号。
    from acoustic_camera.geometry_2d import expected_pair_lags_azel
    from mic16_config import compact32_pairs
    cfg = Mic16Config();pairs = compact32_pairs(cfg)
    lag = expected_pair_lags_azel(*directions[0], cfg, clip=False)
    phase = np.angle(basis[0, 0, pairs[:, 0], pairs[:, 1]])
    expected = np.angle(np.exp(2j*np.pi*1500*lag/cfg.sample_rate))
    phase_error = float(np.max(np.abs(np.angle(np.exp(1j*(phase-expected))))))
    # 既有几何返回float32延迟及单独舍入的成对延迟；
    # 观测到的3.63e-7弧度差来自舍入，不是符号反转。
    assert phase_error < 1e-6
    return dict(status='PASS',covariance_examples=2,acoustic_windows=0,
        reconstruction_max_error=error,explicit_coefficient_error=independent,
        phase_contract_max_error_rad=phase_error,**info)
