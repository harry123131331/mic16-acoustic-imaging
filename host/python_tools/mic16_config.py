# -*- coding: utf-8 -*-
"""Single source of truth for MIC16 Python-tool defaults."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


OFFICIAL_FEATURE_KIND = "gcc_phat"
OFFICIAL_FFT_SIZE = 4096
OFFICIAL_MAX_LAG = 20
OFFICIAL_LAG_COUNT = 41
OFFICIAL_PAIR_COUNT = 32
OFFICIAL_FEATURE_DIM = OFFICIAL_PAIR_COUNT * OFFICIAL_LAG_COUNT
OFFICIAL_FRAME_SAMPLES = 2048
OFFICIAL_SAMPLE_RATE = 39062.5
OFFICIAL_CHANNELS = 16
OFFICIAL_SAMPLES_PER_PACKET = 16
OFFICIAL_ARRAY_RADIUS_M = 0.080
OFFICIAL_ANGLE_STEP_DEG = 5
OFFICIAL_SOUND_SPEED_M_S = 343.0
OFFICIAL_PHAT_EPSILON = 1e-8
OFFICIAL_BANDPASS_LOW_HZ = 300.0
OFFICIAL_BANDPASS_HIGH_HZ = 8000.0
OFFICIAL_BANDPASS_TRANSITION_HZ = 120.0


@dataclass(frozen=True)
class Mic16Config:
    sample_rate: float = OFFICIAL_SAMPLE_RATE
    sound_speed: float = OFFICIAL_SOUND_SPEED_M_S
    channels: int = OFFICIAL_CHANNELS
    array_radius_m: float = OFFICIAL_ARRAY_RADIUS_M
    frame_samples: int = OFFICIAL_FRAME_SAMPLES
    hop_samples: int = OFFICIAL_FRAME_SAMPLES
    fft_size: int = OFFICIAL_FFT_SIZE
    max_lag: int = OFFICIAL_MAX_LAG
    angle_step_deg: int = OFFICIAL_ANGLE_STEP_DEG
    angle_min_deg: int = 0
    angle_max_deg: int = 355
    feature_kind: str = OFFICIAL_FEATURE_KIND
    phat_epsilon: float = OFFICIAL_PHAT_EPSILON
    bandpass_low_hz: float = OFFICIAL_BANDPASS_LOW_HZ
    bandpass_high_hz: float = OFFICIAL_BANDPASS_HIGH_HZ
    bandpass_transition_hz: float = OFFICIAL_BANDPASS_TRANSITION_HZ

    @property
    def lag_count(self) -> int:
        return 2 * int(self.max_lag) + 1

    @property
    def feature_dim(self) -> int:
        return OFFICIAL_PAIR_COUNT * self.lag_count


def angle_grid_deg(cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    return np.arange(
        cfg.angle_min_deg,
        cfg.angle_max_deg + cfg.angle_step_deg,
        cfg.angle_step_deg,
        dtype=np.float32,
    )


def compact32_pairs(cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    pairs: list[tuple[int, int]] = []
    n = int(cfg.channels)
    for i in range(n):
        pairs.append((i, (i + 1) % n))
    for i in range(n // 2):
        pairs.append((i, (i + n // 2) % n))
    for i in range(n // 2):
        pairs.append((i, (i + n // 4) % n))
    return np.asarray(pairs, dtype=np.int64)


def mic_positions_xy(cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    angles = np.arange(cfg.channels, dtype=np.float32) * (2.0 * np.pi / cfg.channels)
    return np.stack(
        [
            float(cfg.array_radius_m) * np.cos(angles),
            float(cfg.array_radius_m) * np.sin(angles),
        ],
        axis=1,
    ).astype(np.float32)
