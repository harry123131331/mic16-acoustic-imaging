# -*- coding: utf-8 -*-
"""Formal MIC16 geometry, angle, TDOA, GCC lag, SRP and DAS convention."""

from __future__ import annotations

import hashlib
import json
from typing import Iterable

import numpy as np

from mic16_channel_map import CHANNEL_MAP_CONVENTION, channel_map_hash, default_channel_map, validate_channel_map
from mic16_config import Mic16Config, angle_grid_deg, compact32_pairs, mic_positions_xy


GEOMETRY_CONTRACT_VERSION = "mic16_circle16_geometry_v1"
LAG_CONVENTION = "pair(a,b) positive lag means channel b arrives later than channel a"
ANGLE_CONVENTION = "0deg is MIC0/+X direction, positive angle is counter-clockwise, angle points from array center to source"
DAS_DELAY_CONVENTION = "delay each channel by negative relative arrival delay to align a center-to-source steering angle"


def get_mic_positions(cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    return mic_positions_xy(cfg)


def get_pair_definitions(cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    return compact32_pairs(cfg)


def source_unit_vector(angle_deg: float) -> np.ndarray:
    theta = np.deg2rad(float(angle_deg))
    return np.asarray([np.cos(theta), np.sin(theta)], dtype=np.float32)


def arrival_delays_samples(angle_deg: float,
                           cfg: Mic16Config = Mic16Config(),
                           distance_m: float | None = None) -> np.ndarray:
    pos = get_mic_positions(cfg)
    if distance_m is None:
        # 远场平面波：越靠近声源方向的麦克风越早接收。
        delays = -(pos @ source_unit_vector(angle_deg)) / float(cfg.sound_speed) * float(cfg.sample_rate)
    else:
        src = source_unit_vector(angle_deg) * float(distance_m)
        dist = np.linalg.norm(pos - src[None, :], axis=1)
        delays = dist / float(cfg.sound_speed) * float(cfg.sample_rate)
    delays = delays - float(np.mean(delays))
    return delays.astype(np.float32)


def expected_tdoa_samples(pair: Iterable[int],
                          angle_deg: float,
                          cfg: Mic16Config = Mic16Config(),
                          distance_m: float | None = None,
                          clip: bool = False) -> float:
    a, b = [int(v) for v in pair]
    delays = arrival_delays_samples(angle_deg, cfg=cfg, distance_m=distance_m)
    lag = float(delays[b] - delays[a])
    if clip:
        lag = max(-float(cfg.max_lag), min(float(cfg.max_lag), lag))
    return lag


def expected_gcc_lag_samples(pair: Iterable[int],
                             angle_deg: float,
                             cfg: Mic16Config = Mic16Config(),
                             distance_m: float | None = None,
                             clip: bool = True) -> float:
    return expected_tdoa_samples(pair, angle_deg, cfg=cfg, distance_m=distance_m, clip=clip)


def geometry_hash(cfg: Mic16Config = Mic16Config(),
                  channel_map: Iterable[int] | np.ndarray | None = None) -> str:
    return hashlib.sha256(
        json.dumps(
            geometry_contract(cfg, channel_map=channel_map, include_hash=False),
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
    ).hexdigest()


def geometry_contract(cfg: Mic16Config = Mic16Config(),
                      channel_map: Iterable[int] | np.ndarray | None = None,
                      include_hash: bool = True) -> dict[str, object]:
    cmap = default_channel_map(cfg) if channel_map is None else validate_channel_map(channel_map, cfg)
    pairs = get_pair_definitions(cfg)
    contract: dict[str, object] = {
        "contract_version": GEOMETRY_CONTRACT_VERSION,
        "sample_rate_hz": float(cfg.sample_rate),
        "channels": int(cfg.channels),
        "array_radius_m": float(cfg.array_radius_m),
        "sound_speed_m_s": float(cfg.sound_speed),
        "frame_samples": int(cfg.frame_samples),
        "fft_size": int(cfg.fft_size),
        "max_lag": int(cfg.max_lag),
        "lag_order": "ascending_-max_to_+max",
        "lag_convention": LAG_CONVENTION,
        "angle_convention": ANGLE_CONVENTION,
        "das_delay_convention": DAS_DELAY_CONVENTION,
        "mic_positions_m": get_mic_positions(cfg).astype(float).tolist(),
        "angle_grid_deg": angle_grid_deg(cfg).astype(float).tolist(),
        "pair_list": pairs.astype(int).tolist(),
        "channel_map_convention": CHANNEL_MAP_CONVENTION,
        "channel_map": cmap.astype(int).tolist(),
        "channel_map_hash": channel_map_hash(cmap, cfg),
        "srp_lag_interpolation": "linear_fractional_lag",
    }
    if include_hash:
        contract["geometry_hash"] = geometry_hash(cfg, cmap)
    return contract


def build_srp_lag_table(cfg: Mic16Config = Mic16Config(),
                        pairs: np.ndarray | None = None,
                        angles_deg: np.ndarray | None = None,
                        range_candidates_m: Iterable[float] | None = None,
                        channel_delay_offsets: Iterable[float] | np.ndarray | None = None) -> np.ndarray:
    if pairs is None:
        pairs = get_pair_definitions(cfg)
    if angles_deg is None:
        angles_deg = angle_grid_deg(cfg)
    ranges = [None] if range_candidates_m is None else [float(x) for x in range_candidates_m]
    offsets = np.zeros(int(cfg.channels), dtype=np.float32)
    if channel_delay_offsets is not None:
        offsets = np.asarray(list(channel_delay_offsets), dtype=np.float32)
        if offsets.shape[0] != int(cfg.channels):
            raise ValueError("channel_delay_offsets must contain one value per channel")
    table = np.empty((len(ranges), len(angles_deg), len(pairs)), dtype=np.float32)
    for ri, distance_m in enumerate(ranges):
        for ai, angle in enumerate(angles_deg):
            for pi, pair in enumerate(np.asarray(pairs, dtype=np.int64)):
                a, b = int(pair[0]), int(pair[1])
                lag = expected_gcc_lag_samples(pair, float(angle), cfg=cfg, distance_m=distance_m, clip=True)
                lag += float(offsets[b] - offsets[a])
                table[ri, ai, pi] = max(-float(cfg.max_lag), min(float(cfg.max_lag), lag))
    return table
