# -*- coding: utf-8 -*-
"""MIC16 raw UDP channel to logical microphone mapping."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Iterable

import numpy as np

from mic16_config import Mic16Config


CHANNEL_MAP_CONVENTION = "channel_map[logical_mic_id]=udp_raw_channel_id"
CHANNEL_MAP_VERSION = "mic16_channel_map_v1"


def default_channel_map(cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    return np.arange(int(cfg.channels), dtype=np.int64)


def validate_channel_map(channel_map: Iterable[int] | np.ndarray,
                         cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    arr = np.asarray(list(channel_map), dtype=np.int64).reshape(-1)
    if arr.shape[0] != int(cfg.channels):
        raise ValueError(f"channel_map must contain {cfg.channels} entries, got {arr.shape[0]}")
    expected = list(range(int(cfg.channels)))
    if sorted(arr.astype(int).tolist()) != expected:
        raise ValueError(f"channel_map must be a permutation of {expected}, got {arr.tolist()}")
    return arr


def parse_channel_map(value: str | Iterable[int] | np.ndarray | None,
                      cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    if value is None:
        return default_channel_map(cfg)
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return default_channel_map(cfg)
        path = Path(text)
        if path.exists() and path.is_file():
            data = json.loads(path.read_text(encoding="utf-8"))
            text_value = data.get("channel_map", data.get("map", []))
            return validate_channel_map(text_value, cfg)
        try:
            parsed = json.loads(text)
            if isinstance(parsed, dict):
                parsed = parsed.get("channel_map", parsed.get("map", []))
        except json.JSONDecodeError:
            parsed = [int(part) for part in text.replace(";", ",").replace(",", " ").split()]
        return validate_channel_map(parsed, cfg)
    return validate_channel_map(value, cfg)


def apply_channel_map(raw_pcm: np.ndarray,
                      channel_map: Iterable[int] | np.ndarray | None = None,
                      cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    """Return logical MIC0..MIC15 data from raw UDP-order PCM.

    Semantics:
        logical_pcm[:, logical_mic_id] = raw_pcm[:, channel_map[logical_mic_id]]
    """
    x = np.asarray(raw_pcm)
    if x.ndim < 2:
        raise ValueError("raw_pcm must have a channel dimension")
    if x.shape[-1] != int(cfg.channels):
        raise ValueError(f"raw_pcm last dimension must be {cfg.channels}, got {x.shape}")
    cmap = default_channel_map(cfg) if channel_map is None else validate_channel_map(channel_map, cfg)
    return np.ascontiguousarray(x[..., cmap])


def invert_channel_map(channel_map: Iterable[int] | np.ndarray,
                       cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    cmap = validate_channel_map(channel_map, cfg)
    inv = np.empty_like(cmap)
    for logical_id, raw_id in enumerate(cmap):
        inv[int(raw_id)] = int(logical_id)
    return inv


def channel_map_hash(channel_map: Iterable[int] | np.ndarray | None = None,
                     cfg: Mic16Config = Mic16Config()) -> str:
    cmap = default_channel_map(cfg) if channel_map is None else validate_channel_map(channel_map, cfg)
    payload = {
        "version": CHANNEL_MAP_VERSION,
        "convention": CHANNEL_MAP_CONVENTION,
        "channels": int(cfg.channels),
        "channel_map": cmap.astype(int).tolist(),
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
