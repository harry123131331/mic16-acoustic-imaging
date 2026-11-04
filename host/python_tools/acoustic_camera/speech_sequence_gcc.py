"""Experimental sequence-level GCC-PHAT frontends for continuous speech.

The frozen MIC16 feature contract performs PHAT independently on every
2048-sample frame.  That contract is intentionally left unchanged.  This
module provides isolated, explicitly versioned alternatives for the PC-side
Az/El room experiment, where several consecutive frames may be integrated
before the PHAT normalization.
"""

from __future__ import annotations

from typing import Iterable, Literal

import numpy as np

from mic16_channel_map import apply_channel_map, default_channel_map, validate_channel_map
from mic16_config import Mic16Config, compact32_pairs


SPEECH_SEQUENCE_GCC_VERSION = "mic16_speech_sequence_gcc_phat_v1"
SequenceGccMode = Literal["frame_mean", "cross_spectrum_accumulate", "long_window"]


def _next_power_of_two(value: int) -> int:
    if int(value) <= 0:
        raise ValueError("value must be positive")
    return 1 << (int(value) - 1).bit_length()


def _validate_frames(frames: np.ndarray, cfg: Mic16Config) -> np.ndarray:
    values = np.asarray(frames, dtype=np.float64)
    expected = (int(cfg.frame_samples), int(cfg.channels))
    if values.ndim != 3 or values.shape[1:] != expected:
        raise ValueError(
            "frames must have shape [frame_count,"
            f"{cfg.frame_samples},{cfg.channels}], got {values.shape}"
        )
    if values.shape[0] < 1:
        raise ValueError("at least one frame is required")
    if not np.all(np.isfinite(values)):
        raise ValueError("frames contain non-finite values")
    return values


def _frequency_mask(
    fft_size: int,
    cfg: Mic16Config,
    bandpass: bool,
) -> np.ndarray:
    if not bandpass:
        return np.ones(fft_size // 2 + 1, dtype=bool)
    frequencies = np.fft.rfftfreq(int(fft_size), d=1.0 / float(cfg.sample_rate))
    return (
        (frequencies >= float(cfg.bandpass_low_hz))
        & (frequencies <= float(cfg.bandpass_high_hz))
    )


def _lag_window(cross_spectrum: np.ndarray, fft_size: int, cfg: Mic16Config) -> np.ndarray:
    cross_phat = cross_spectrum / (np.abs(cross_spectrum) + float(cfg.phat_epsilon))
    correlation = np.fft.irfft(cross_phat, n=int(fft_size))
    lags = np.arange(-int(cfg.max_lag), int(cfg.max_lag) + 1, dtype=np.int64)
    return correlation[(-lags) % int(fft_size)].astype(np.float32)


def extract_sequence_gcc_phat_features(
    frames: np.ndarray,
    cfg: Mic16Config = Mic16Config(),
    *,
    mode: SequenceGccMode = "cross_spectrum_accumulate",
    pairs: np.ndarray | None = None,
    channel_map: Iterable[int] | np.ndarray | None = None,
    bandpass: bool = True,
    window: Literal["rectangular", "hann"] = "hann",
) -> np.ndarray:
    """Return one ``[pair, lag]`` feature matrix for a frame sequence.

    ``frame_mean`` is an experimental reimplementation of the frozen
    per-frame frontend followed by curve averaging.  The other two modes
    normalize only after temporal integration: either by accumulating the
    per-frame cross spectra, or by concatenating the sequence into a single
    linear-correlation window.  A 300--8000 Hz mask is enabled by default for
    these speech-only experiments and is recorded by every caller.
    """

    values = _validate_frames(frames, cfg)
    cmap = default_channel_map(cfg) if channel_map is None else validate_channel_map(channel_map, cfg)
    logical = np.stack([apply_channel_map(frame, cmap, cfg) for frame in values], axis=0)
    pair_array = compact32_pairs(cfg) if pairs is None else np.asarray(pairs, dtype=np.int64)
    if pair_array.ndim != 2 or pair_array.shape[1] != 2:
        raise ValueError("pairs must have shape [pair_count,2]")
    if np.any(pair_array < 0) or np.any(pair_array >= int(cfg.channels)):
        raise ValueError("pairs contain an out-of-range channel index")
    if mode not in {"frame_mean", "cross_spectrum_accumulate", "long_window"}:
        raise ValueError(f"unsupported sequence GCC mode: {mode}")
    if window not in {"rectangular", "hann"}:
        raise ValueError(f"unsupported window: {window}")

    lag_count = 2 * int(cfg.max_lag) + 1
    output = np.empty((pair_array.shape[0], lag_count), dtype=np.float32)

    if mode == "long_window":
        joined = logical.reshape(-1, int(cfg.channels))
        joined = joined - np.mean(joined, axis=0, keepdims=True)
        taper = np.hanning(joined.shape[0]) if window == "hann" else np.ones(joined.shape[0])
        joined = joined * taper[:, None]
        fft_size = _next_power_of_two(2 * joined.shape[0])
        spectra = np.fft.rfft(joined, n=fft_size, axis=0)
        mask = _frequency_mask(fft_size, cfg, bandpass)
        for pair_index, (channel_a, channel_b) in enumerate(pair_array):
            cross = spectra[:, int(channel_a)] * np.conj(spectra[:, int(channel_b)])
            cross[~mask] = 0.0
            output[pair_index] = _lag_window(cross, fft_size, cfg)
        return output

    frame_length = int(cfg.frame_samples)
    fft_size = int(cfg.fft_size)
    if fft_size < frame_length:
        raise ValueError("cfg.fft_size is shorter than cfg.frame_samples")
    logical = logical - np.mean(logical, axis=1, keepdims=True)
    taper = np.hanning(frame_length) if window == "hann" else np.ones(frame_length)
    spectra = np.fft.rfft(logical * taper[None, :, None], n=fft_size, axis=1)
    mask = _frequency_mask(fft_size, cfg, bandpass)
    for pair_index, (channel_a, channel_b) in enumerate(pair_array):
        cross_per_frame = (
            spectra[:, :, int(channel_a)] * np.conj(spectra[:, :, int(channel_b)])
        )
        cross_per_frame[:, ~mask] = 0.0
        if mode == "cross_spectrum_accumulate":
            output[pair_index] = _lag_window(np.sum(cross_per_frame, axis=0), fft_size, cfg)
        else:
            curves = np.stack(
                [_lag_window(cross, fft_size, cfg) for cross in cross_per_frame], axis=0
            )
            output[pair_index] = np.mean(curves, axis=0).astype(np.float32)
    return output

