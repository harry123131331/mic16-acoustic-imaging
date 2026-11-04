"""Frame features shared by the experimental MIC16 Az/El data and runtime."""

from __future__ import annotations

import numpy as np

from mic16_config import Mic16Config

from .srp_phat_2d import srp_phat_azel_response


AZEL_FEATURE_VERSION = "mic16_azel_srp_quality_frame_v1"


def pair_reliability_features(gcc_features: np.ndarray) -> np.ndarray:
    """Return bounded non-oracle peak-quality diagnostics for every pair."""

    curves = np.asarray(gcc_features, dtype=np.float32)
    if curves.ndim != 2:
        raise ValueError("gcc_features must have shape [pair,lag]")
    peak = np.max(curves, axis=1)
    median = np.median(curves, axis=1)
    low = np.percentile(curves, 10.0, axis=1)
    high = np.percentile(curves, 90.0, axis=1)
    prominence = np.maximum(peak - median, 0.0)
    spread = np.maximum(high - low, 1e-8)
    reliability = prominence / (prominence + spread)
    return np.clip(reliability, 0.0, 1.0).astype(np.float32)


def global_quality_features(
    response: np.ndarray,
    pair_reliability: np.ndarray,
) -> np.ndarray:
    """Return six bounded/scale-stable diagnostics without using labels."""

    values = np.asarray(response, dtype=np.float64).reshape(-1)
    reliability = np.asarray(pair_reliability, dtype=np.float64).reshape(-1)
    if values.size == 0 or reliability.size == 0:
        raise ValueError("response and pair_reliability must be non-empty")
    shifted = values - float(np.min(values))
    probability = shifted + 1e-10
    probability /= float(np.sum(probability))
    entropy = -float(np.sum(probability * np.log(probability)))
    normalized_entropy = entropy / max(float(np.log(values.size)), 1e-10)
    peak = float(np.max(values))
    median = float(np.median(values))
    spread = float(np.percentile(values, 95.0) - np.percentile(values, 5.0))
    prominence = max(peak - median, 0.0)
    peak_sharpness = prominence / (prominence + max(spread, 1e-8))
    sorted_values = np.sort(values)
    second = float(sorted_values[-2]) if sorted_values.size >= 2 else peak
    peak_separation = max(peak - second, 0.0) / (abs(peak) + abs(second) + 1e-8)
    return np.asarray(
        [
            peak_sharpness,
            peak_separation,
            1.0 - np.clip(normalized_entropy, 0.0, 1.0),
            float(np.mean(reliability)),
            float(np.min(reliability)),
            float(np.mean(reliability >= 0.20)),
        ],
        dtype=np.float32,
    )


def build_azel_frame_features(
    gcc_features: np.ndarray,
    lag_table: np.ndarray,
    cfg: Mic16Config = Mic16Config(),
) -> dict[str, np.ndarray | int]:
    """Build one model frame from formal GCC-PHAT pair curves."""

    response, active_pair_count = srp_phat_azel_response(
        gcc_features, lag_table, cfg
    )
    reliability = pair_reliability_features(gcc_features)
    global_quality = global_quality_features(response, reliability)
    concatenated = np.concatenate(
        [response.reshape(-1), reliability, global_quality]
    ).astype(np.float32)
    return {
        "srp_response": response.astype(np.float32),
        "pair_reliability": reliability,
        "global_quality": global_quality,
        "feature_vector": concatenated,
        "active_pair_count": int(active_pair_count),
    }
