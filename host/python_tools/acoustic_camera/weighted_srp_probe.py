"""Research-only frequency weighting for the DEV-8 TRAIN candidate-recall probe.

This does not alter the frozen official GCC-PHAT frontend.  The two variants
keep its FFT, channel map, pair list, PHAT convention and lag indexing; only
the frequency-bin weights differ.  The noise-aware weight is not a trained
speech mask and must not be described as one.
"""

from __future__ import annotations

from typing import Literal

import numpy as np

from mic16_channel_map import apply_channel_map, default_channel_map
from mic16_config import Mic16Config, compact32_pairs


ProbeMode = Literal["speech_band", "noise_excess"]


def _spectrum(frame: np.ndarray, cfg: Mic16Config) -> np.ndarray:
    values = np.asarray(frame, dtype=np.float32)
    if values.shape != (cfg.frame_samples, cfg.channels) or not np.isfinite(values).all():
        raise ValueError("frame must be finite [frame_samples, channels]")
    logical = apply_channel_map(values, default_channel_map(cfg), cfg)
    logical = logical - np.mean(logical, axis=0, keepdims=True)
    return np.fft.rfft(logical, n=cfg.fft_size, axis=0)


def calibration_noise_psd(frames: list[np.ndarray], cfg: Mic16Config) -> np.ndarray:
    """Median across calibration frames and microphones, with no truth access."""
    if not frames:
        raise ValueError("at least one calibration frame is required")
    powers = np.stack([np.abs(_spectrum(frame, cfg)) ** 2 for frame in frames])
    return np.maximum(np.median(powers, axis=(0, 2)), 1e-20)


def frequency_weights(
    frame: np.ndarray,
    cfg: Mic16Config,
    mode: ProbeMode,
    noise_psd: np.ndarray | None = None,
) -> np.ndarray:
    frequencies = np.fft.rfftfreq(cfg.fft_size, d=1.0 / cfg.sample_rate)
    band = (frequencies >= cfg.bandpass_low_hz) & (frequencies <= cfg.bandpass_high_hz)
    if mode == "speech_band":
        return band.astype(np.float64)
    if mode != "noise_excess":
        raise ValueError(f"unknown probe mode: {mode}")
    noise = np.asarray(noise_psd, dtype=np.float64)
    if noise.shape != frequencies.shape or not np.isfinite(noise).all() or np.any(noise <= 0):
        raise ValueError("noise_psd must be finite, positive and match the FFT")
    signal = np.median(np.abs(_spectrum(frame, cfg)) ** 2, axis=1)
    excess = np.clip((signal - noise) / np.maximum(signal, 1e-20), 0.0, 1.0)
    # 保留小残差，避免低信噪比语音频点被置为精确零。
    return band.astype(np.float64) * (0.05 + 0.95 * excess)


def weighted_gcc_phat(
    frame: np.ndarray,
    cfg: Mic16Config,
    mode: ProbeMode,
    noise_psd: np.ndarray | None = None,
) -> tuple[np.ndarray, np.ndarray]:
    """Return [32,41] GCC curves and the auditable FFT-bin weight vector."""
    spectra = _spectrum(frame, cfg)
    weights = frequency_weights(frame, cfg, mode, noise_psd)
    pairs = compact32_pairs(cfg)
    cross = spectra[:, pairs[:, 0]] * np.conj(spectra[:, pairs[:, 1]])
    phat = cross / (np.abs(cross) + cfg.phat_epsilon)
    correlations = np.fft.irfft(phat * weights[:, None], n=cfg.fft_size, axis=0)
    lags = np.arange(-cfg.max_lag, cfg.max_lag + 1)
    gcc = correlations[(-lags) % cfg.fft_size, :].T.astype(np.float32)
    return gcc, weights
