"""Lightweight auditable target-speaker reference features for experiments.

This module is deliberately self-contained and is not a substitute for a
trained speaker-verification model.  It provides an MFCC-statistics baseline
that can be inspected and run without downloading external checkpoints.
"""

from __future__ import annotations

from fractions import Fraction

import numpy as np
from scipy.fft import dct
from scipy.signal import resample_poly

from mic16_config import Mic16Config

from .geometry_2d import far_field_arrival_delays_samples_azel


VOICEPRINT_VERSION = "mic16_mfcc_statistics_voiceprint_v1"
VOICEPRINT_SAMPLE_RATE = 16000
MEL_BAND_COUNT = 40
CEPSTRAL_COUNT = 20


def resample_audio(signal: np.ndarray, source_rate: float, target_rate: int = VOICEPRINT_SAMPLE_RATE) -> np.ndarray:
    values = np.asarray(signal, dtype=np.float64)
    if values.ndim == 1:
        values = values[:, None]
    if values.ndim != 2 or values.shape[0] == 0:
        raise ValueError("signal must be [sample] or [sample,channel]")
    ratio = Fraction(float(target_rate) / float(source_rate)).limit_denominator(10000)
    output = resample_poly(values, ratio.numerator, ratio.denominator, axis=0)
    return np.asarray(output[:, 0] if signal.ndim == 1 else output, dtype=np.float32)


def active_excerpt(signal: np.ndarray, sample_rate: int, maximum_seconds: float = 6.0) -> np.ndarray:
    values = np.asarray(signal, dtype=np.float32).reshape(-1)
    maximum = max(1, int(round(float(maximum_seconds) * float(sample_rate))))
    if values.size <= maximum:
        return values
    squared = np.square(values, dtype=np.float64)
    cumulative = np.concatenate([[0.0], np.cumsum(squared)])
    window_energy = cumulative[maximum:] - cumulative[:-maximum]
    start = int(np.argmax(window_energy))
    return values[start : start + maximum]


def _hz_to_mel(frequency_hz: np.ndarray | float) -> np.ndarray:
    return 2595.0 * np.log10(1.0 + np.asarray(frequency_hz, dtype=np.float64) / 700.0)


def _mel_to_hz(mel: np.ndarray | float) -> np.ndarray:
    return 700.0 * (np.power(10.0, np.asarray(mel, dtype=np.float64) / 2595.0) - 1.0)


def mel_filterbank(sample_rate: int, n_fft: int, band_count: int = MEL_BAND_COUNT) -> np.ndarray:
    low_mel = float(_hz_to_mel(80.0))
    high_mel = float(_hz_to_mel(min(7600.0, 0.475 * float(sample_rate))))
    points_hz = _mel_to_hz(np.linspace(low_mel, high_mel, int(band_count) + 2))
    bins = np.floor((n_fft + 1) * points_hz / float(sample_rate)).astype(int)
    bins = np.clip(bins, 0, n_fft // 2)
    filters = np.zeros((int(band_count), n_fft // 2 + 1), dtype=np.float64)
    for index in range(int(band_count)):
        left, center, right = int(bins[index]), int(bins[index + 1]), int(bins[index + 2])
        center = max(center, left + 1)
        right = max(right, center + 1)
        for frequency_bin in range(left, min(center, filters.shape[1])):
            filters[index, frequency_bin] = (frequency_bin - left) / float(center - left)
        for frequency_bin in range(center, min(right, filters.shape[1])):
            filters[index, frequency_bin] = (right - frequency_bin) / float(right - center)
    return filters.astype(np.float32)


def voiceprint(signal: np.ndarray, sample_rate: int = VOICEPRINT_SAMPLE_RATE) -> np.ndarray:
    values = np.asarray(signal, dtype=np.float32).reshape(-1)
    if values.size < int(0.08 * sample_rate) or not np.isfinite(values).all():
        raise ValueError("voiceprint input is too short or non-finite")
    values = values - float(np.mean(values))
    scale = float(np.sqrt(np.mean(np.square(values)) + 1e-12))
    values = values / max(scale, 1e-8)
    emphasized = np.concatenate([[values[0]], values[1:] - 0.97 * values[:-1]])
    frame_length = int(round(0.025 * sample_rate))
    hop = int(round(0.010 * sample_rate))
    frame_count = 1 + max(0, (emphasized.size - frame_length) // hop)
    indices = np.arange(frame_length)[None, :] + hop * np.arange(frame_count)[:, None]
    frames = emphasized[indices] * np.hanning(frame_length)[None, :]
    n_fft = 512
    power = np.square(np.abs(np.fft.rfft(frames, n=n_fft, axis=1))) / float(n_fft)
    log_mel = np.log(np.maximum(power @ mel_filterbank(sample_rate, n_fft).T, 1e-10))
    cepstra = dct(log_mel, type=2, axis=1, norm="ortho")[:, 1 : CEPSTRAL_COUNT + 1]
    delta = np.diff(cepstra, axis=0)
    if delta.shape[0] == 0:
        delta = np.zeros_like(cepstra)
    feature = np.concatenate(
        [
            np.mean(cepstra, axis=0),
            np.std(cepstra, axis=0),
            np.mean(np.abs(delta), axis=0),
            np.std(delta, axis=0),
        ]
    ).astype(np.float32)
    norm = float(np.linalg.norm(feature))
    if norm <= 1e-8:
        raise ValueError("voiceprint feature is degenerate")
    return feature / norm


def cosine_similarity(left: np.ndarray, right: np.ndarray) -> float:
    a = np.asarray(left, dtype=np.float32).reshape(-1)
    b = np.asarray(right, dtype=np.float32).reshape(-1)
    if a.shape != b.shape:
        raise ValueError("voiceprint shapes do not match")
    return float(np.dot(a, b) / max(float(np.linalg.norm(a) * np.linalg.norm(b)), 1e-8))


def delay_and_sum_azel(
    sequence: np.ndarray,
    azimuth_deg: float,
    elevation_deg: float,
    cfg: Mic16Config = Mic16Config(),
    output_sample_rate: int = VOICEPRINT_SAMPLE_RATE,
) -> np.ndarray:
    values = np.asarray(sequence, dtype=np.float32)
    if values.ndim == 3:
        values = values.reshape(-1, values.shape[-1])
    if values.ndim != 2 or values.shape[1] != int(cfg.channels):
        raise ValueError(f"sequence must have 16 channels, got {values.shape}")
    downsampled = resample_audio(values, float(cfg.sample_rate), int(output_sample_rate))
    return delay_and_sum_azel_resampled(
        downsampled,
        azimuth_deg,
        elevation_deg,
        cfg=cfg,
        sample_rate=int(output_sample_rate),
    )


def delay_and_sum_azel_resampled(
    multichannel: np.ndarray,
    azimuth_deg: float,
    elevation_deg: float,
    cfg: Mic16Config = Mic16Config(),
    sample_rate: int = VOICEPRINT_SAMPLE_RATE,
) -> np.ndarray:
    """Beamform an already resampled multichannel sequence."""

    downsampled = np.asarray(multichannel, dtype=np.float32)
    if downsampled.ndim != 2 or downsampled.shape[1] != int(cfg.channels):
        raise ValueError(f"multichannel must have 16 channels, got {downsampled.shape}")
    delays = far_field_arrival_delays_samples_azel(
        float(azimuth_deg), float(elevation_deg), cfg
    ).astype(np.float64)
    delays *= float(sample_rate) / float(cfg.sample_rate)
    axis = np.arange(downsampled.shape[0], dtype=np.float64)
    aligned = np.empty_like(downsampled, dtype=np.float32)
    for channel in range(int(cfg.channels)):
        aligned[:, channel] = np.interp(
            axis + float(delays[channel]),
            axis,
            downsampled[:, channel],
            left=0.0,
            right=0.0,
        )
    margin = max(2, int(np.ceil(np.max(np.abs(delays)))) + 2)
    if aligned.shape[0] <= 2 * margin:
        raise ValueError("beamformed sequence is too short after edge cropping")
    return np.mean(aligned[margin:-margin], axis=1).astype(np.float32)
