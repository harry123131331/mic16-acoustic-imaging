# -*- coding: utf-8 -*-
"""Formal GCC-PHAT feature extraction for MIC16.

This is the official feature frontend for training and evaluation:
16ch PCM -> channel_map -> per-channel DC removal -> RFFT -> cross spectrum
-> PHAT normalization -> IFFT -> lag window [-20, +20].
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Iterable

import numpy as np

from mic16_channel_map import CHANNEL_MAP_CONVENTION, apply_channel_map, channel_map_hash, default_channel_map, validate_channel_map
from mic16_config import (
    Mic16Config,
    OFFICIAL_FEATURE_DIM,
    OFFICIAL_FEATURE_KIND,
    OFFICIAL_FFT_SIZE,
    OFFICIAL_MAX_LAG,
    OFFICIAL_PAIR_COUNT,
    angle_grid_deg,
    compact32_pairs,
)
from mic16_geometry_convention import GEOMETRY_CONTRACT_VERSION, LAG_CONVENTION, geometry_hash


GCC_PHAT_FEATURE_VERSION = "gcc_phat_fft4096_phat_ifft_lagwin_v1"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def remove_dc(frame: np.ndarray) -> np.ndarray:
    x = np.asarray(frame, dtype=np.float32)
    if x.ndim != 2:
        raise ValueError(f"frame must have shape [samples, channels], got {x.shape}")
    return (x - np.mean(x, axis=0, keepdims=True)).astype(np.float32)


def extract_gcc_phat_pair(sig_a: np.ndarray,
                          sig_b: np.ndarray,
                          max_lag: int = OFFICIAL_MAX_LAG,
                          fft_size: int = OFFICIAL_FFT_SIZE,
                          phat_epsilon: float = 1e-8) -> np.ndarray:
    """Return GCC-PHAT lag window in order ``-max_lag ... +max_lag``.

    For pair(a,b), positive lag means channel b arrives later than channel a.
    No time-domain NCC or per-pair peak normalization is used.
    """
    a = np.asarray(sig_a, dtype=np.float32).reshape(-1)
    b = np.asarray(sig_b, dtype=np.float32).reshape(-1)
    if a.shape[0] != b.shape[0]:
        raise ValueError("pair signals must have the same length")
    if int(fft_size) < a.shape[0]:
        raise ValueError(f"fft_size={fft_size} is shorter than frame length={a.shape[0]}")
    a = a - float(np.mean(a))
    b = b - float(np.mean(b))
    fa = np.fft.rfft(a, n=int(fft_size))
    fb = np.fft.rfft(b, n=int(fft_size))
    cross = fa * np.conj(fb)
    cross_phat = cross / (np.abs(cross) + float(phat_epsilon))
    corr = np.fft.irfft(cross_phat, n=int(fft_size)).astype(np.float32)
    lags = np.arange(-int(max_lag), int(max_lag) + 1, dtype=np.int64)
    # 上述约定下，X_a乘X_b共轭的互相关峰位于-lag。
    return corr[(-lags) % int(fft_size)].astype(np.float32)


def extract_gcc_phat_features(frame: np.ndarray,
                              cfg: Mic16Config = Mic16Config(),
                              pairs: np.ndarray | None = None,
                              channel_map: Iterable[int] | np.ndarray | None = None,
                              flatten: bool = False) -> np.ndarray:
    x = np.asarray(frame)
    if x.ndim != 2:
        raise ValueError(f"frame must have shape [samples, channels], got {x.shape}")
    if x.shape[0] != int(cfg.frame_samples):
        raise ValueError(f"expected {cfg.frame_samples} samples, got {x.shape[0]}")
    if x.shape[1] != int(cfg.channels):
        raise ValueError(f"expected {cfg.channels} channels, got {x.shape[1]}")
    cmap = default_channel_map(cfg) if channel_map is None else validate_channel_map(channel_map, cfg)
    logical = apply_channel_map(x, cmap, cfg)
    logical = remove_dc(logical)
    if pairs is None:
        pairs = compact32_pairs(cfg)
    pairs = np.asarray(pairs, dtype=np.int64)
    features = np.empty((pairs.shape[0], 2 * int(cfg.max_lag) + 1), dtype=np.float32)
    for idx, (a, b) in enumerate(pairs):
        features[idx] = extract_gcc_phat_pair(
            logical[:, int(a)],
            logical[:, int(b)],
            max_lag=int(cfg.max_lag),
            fft_size=int(cfg.fft_size),
            phat_epsilon=float(cfg.phat_epsilon),
        )
    return features.reshape(-1).astype(np.float32) if flatten else features


def extract_gcc_phat_feature_vector(frame: np.ndarray,
                                    cfg: Mic16Config = Mic16Config(),
                                    pairs: np.ndarray | None = None,
                                    channel_map: Iterable[int] | np.ndarray | None = None) -> np.ndarray:
    return extract_gcc_phat_features(frame, cfg=cfg, pairs=pairs, channel_map=channel_map, flatten=True)


def extract_gcc_phat_feature_batch(frames: np.ndarray,
                                   cfg: Mic16Config = Mic16Config(),
                                   pairs: np.ndarray | None = None,
                                   channel_map: Iterable[int] | np.ndarray | None = None,
                                   flatten: bool = False) -> np.ndarray:
    arr = np.asarray(frames)
    if arr.ndim != 3:
        raise ValueError(f"frames must have shape [frames, samples, channels], got {arr.shape}")
    return np.stack(
        [
            extract_gcc_phat_features(frame, cfg=cfg, pairs=pairs, channel_map=channel_map, flatten=flatten)
            for frame in arr
        ],
        axis=0,
    ).astype(np.float32)


def gcc_phat_feature_metadata(cfg: Mic16Config = Mic16Config(),
                              channel_map: Iterable[int] | np.ndarray | None = None) -> dict[str, object]:
    cmap = default_channel_map(cfg) if channel_map is None else validate_channel_map(channel_map, cfg)
    pairs = compact32_pairs(cfg)
    return {
        "feature_kind": OFFICIAL_FEATURE_KIND,
        "feature_formula_version": GCC_PHAT_FEATURE_VERSION,
        "fft_size": int(OFFICIAL_FFT_SIZE),
        "phat_epsilon": float(cfg.phat_epsilon),
        "max_lag": int(OFFICIAL_MAX_LAG),
        "lag_count": int(2 * cfg.max_lag + 1),
        "lag_order": "ascending_-max_to_+max",
        "lag_convention": LAG_CONVENTION,
        "pair_count": int(OFFICIAL_PAIR_COUNT),
        "feature_shape": [int(OFFICIAL_PAIR_COUNT), int(2 * cfg.max_lag + 1)],
        "feature_dim": int(OFFICIAL_FEATURE_DIM),
        "sample_rate_hz": float(cfg.sample_rate),
        "frame_samples": int(cfg.frame_samples),
        "hop_samples": int(cfg.hop_samples),
        "channels": int(cfg.channels),
        "array_radius_m": float(cfg.array_radius_m),
        "angle_labels_deg": angle_grid_deg(cfg).astype(float).tolist(),
        "pair_list": pairs.astype(int).tolist(),
        "channel_map_convention": CHANNEL_MAP_CONVENTION,
        "channel_map": cmap.astype(int).tolist(),
        "channel_map_hash": channel_map_hash(cmap, cfg),
        "geometry_contract_version": GEOMETRY_CONTRACT_VERSION,
        "geometry_hash": geometry_hash(cfg, cmap),
        "normalization": "PHAT normalization only; train split global mean/std may be applied later",
    }
