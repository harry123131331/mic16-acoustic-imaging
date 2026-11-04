# -*- coding: utf-8 -*-
"""Pure operator-status rules shared by the MIC16 GUI and tests."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


SEVERITY_IDLE = "idle"
SEVERITY_GOOD = "good"
SEVERITY_WARN = "warn"
SEVERITY_BAD = "bad"


@dataclass(frozen=True)
class OperatorDecision:
    headline: str
    detail: str
    severity: str


@dataclass(frozen=True)
class ChannelHealth:
    rms: np.ndarray
    dead_mask: np.ndarray
    weak_mask: np.ndarray

    @property
    def healthy_count(self) -> int:
        return int(np.count_nonzero(~self.dead_mask & ~self.weak_mask))

    @property
    def weak_channels(self) -> tuple[int, ...]:
        return tuple(int(v) for v in np.flatnonzero(self.weak_mask & ~self.dead_mask))

    @property
    def dead_channels(self) -> tuple[int, ...]:
        return tuple(int(v) for v in np.flatnonzero(self.dead_mask))


def classify_localization(
    *,
    frame_valid: bool,
    warmup: bool,
    confidence: float,
    confidence_threshold: float,
    invalid_reason: str = "",
    source: str = "",
) -> OperatorDecision:
    source_text = source.strip() or "定位后端"
    if warmup:
        return OperatorDecision("序列预热", f"{source_text} 正在累积连续帧，当前结果不参与定位。", SEVERITY_WARN)
    if not frame_valid:
        reason = invalid_reason.strip() or "quality_gate"
        return OperatorDecision("当前帧无效", f"{source_text} 已拒绝输出：{reason}", SEVERITY_BAD)
    if float(confidence) < float(confidence_threshold):
        return OperatorDecision(
            "低置信度",
            f"{source_text} 置信度 {confidence:.3f}，低于门限 {confidence_threshold:.3f}。",
            SEVERITY_WARN,
        )
    return OperatorDecision(
        "定位有效",
        f"{source_text} 置信度 {confidence:.3f}，结果可用于显示与后续控制。",
        SEVERITY_GOOD,
    )


def analyze_channel_health(samples: np.ndarray, weak_ratio: float = 0.08, dead_ratio: float = 0.005) -> ChannelHealth:
    data = np.asarray(samples, dtype=np.float64)
    if data.ndim != 2:
        raise ValueError(f"samples must have shape [samples, channels], got {data.shape}")
    if data.shape[0] == 0 or data.shape[1] == 0:
        channels = data.shape[1] if data.ndim == 2 else 0
        empty = np.zeros(channels, dtype=np.float64)
        return ChannelHealth(empty, np.ones(channels, dtype=bool), np.zeros(channels, dtype=bool))

    centered = data - np.mean(data, axis=0, keepdims=True)
    rms = np.sqrt(np.mean(centered * centered, axis=0))
    positive = rms[rms > 0.0]
    reference = float(np.median(positive)) if positive.size else 0.0
    if reference <= 0.0:
        return ChannelHealth(rms, np.ones(rms.shape, dtype=bool), np.zeros(rms.shape, dtype=bool))

    dead_mask = rms < max(1.0e-9, reference * float(dead_ratio))
    weak_mask = (rms < reference * float(weak_ratio)) & ~dead_mask
    return ChannelHealth(rms, dead_mask, weak_mask)


def format_channel_health(health: ChannelHealth) -> str:
    parts = [f"健康 {health.healthy_count}/{health.rms.size}"]
    if health.weak_channels:
        parts.append("弱通道 " + ",".join(f"CH{ch}" for ch in health.weak_channels))
    if health.dead_channels:
        parts.append("无信号 " + ",".join(f"CH{ch}" for ch in health.dead_channels))
    return " | ".join(parts)
