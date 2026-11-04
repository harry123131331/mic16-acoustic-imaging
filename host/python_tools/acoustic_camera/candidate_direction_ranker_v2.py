"""Candidate-direction speech-conditioned ranking for the MIC16 research path.

This module is deliberately conditional on a frame already containing speech.
It combines the frozen spatial direction score, the interpretable B6C spatial
features, and the candidate-steered B6S speech features.  Frame speech/no-speech
remains a separate task.  The ranker is software research code and does not
change the formal FPGA ABI.
"""

from __future__ import annotations

from typing import Any

import numpy as np

from .interpretable_candidate_classifier import FEATURE_NAMES as B6C_FEATURE_NAMES
from .probabilistic_speech_presence import SPEECH_PRESENCE_FEATURE_NAMES


CANDIDATE_DIRECTION_RANKER_V2_VERSION = "mic16_candidate_direction_ranker_v2"

FEATURE_NAMES = (
    "frozen_direction_probability",
    "frozen_direction_margin_over_runner_up",
    "upstream_b6c_probability",
    *(f"b6c_{name}" for name in B6C_FEATURE_NAMES),
    *(f"candidate_beam_{name}" for name in SPEECH_PRESENCE_FEATURE_NAMES),
)


def candidate_feature_matrix(row: dict[str, Any]) -> np.ndarray:
    """Return one speech-conditioned direction feature vector per candidate."""

    probabilities = np.asarray(row.get("_direction", ()), dtype=np.float64)
    candidates = row.get("candidates", ())
    if probabilities.size != len(candidates):
        raise ValueError("direction/candidate count mismatch")
    if probabilities.size == 0:
        return np.empty((0, len(FEATURE_NAMES)), dtype=np.float64)
    matrix: list[list[float]] = []
    for index, candidate in enumerate(candidates):
        runner_up = max(
            (float(value) for other, value in enumerate(probabilities) if other != index),
            default=0.0,
        )
        b6c = tuple(float(value) for value in candidate["b6c_features"])
        b6s = tuple(float(value) for value in candidate["b6s_features"])
        if len(b6c) != len(B6C_FEATURE_NAMES) or len(b6s) != len(SPEECH_PRESENCE_FEATURE_NAMES):
            raise ValueError("candidate feature schema mismatch")
        matrix.append(
            [
                float(probabilities[index]),
                float(probabilities[index]) - runner_up,
                float(candidate["b6c_probability"]),
                *b6c,
                *b6s,
            ]
        )
    values = np.asarray(matrix, dtype=np.float64)
    if values.shape != (len(candidates), len(FEATURE_NAMES)) or not np.isfinite(values).all():
        raise RuntimeError("candidate direction feature matrix is invalid")
    return values


def pairwise_training_inputs(
    rows: list[dict[str, Any]],
) -> tuple[list[np.ndarray], list[np.ndarray], list[np.ndarray], list[tuple[str, int]]]:
    """Build positive-vs-hard-negative frames without using no-source labels."""

    feature_frames: list[np.ndarray] = []
    label_frames: list[np.ndarray] = []
    hardness_frames: list[np.ndarray] = []
    keys: list[tuple[str, int]] = []
    for row in rows:
        if not row.get("truths") or not row.get("candidates"):
            continue
        labels = np.asarray(
            [int(item["is_true_speech_candidate"]) for item in row["candidates"]],
            dtype=np.int8,
        )
        if not (np.any(labels == 1) and np.any(labels == 0)):
            continue
        features = candidate_feature_matrix(row)
        hardness = np.asarray(row["_direction"], dtype=np.float64)
        feature_frames.append(features)
        label_frames.append(labels)
        hardness_frames.append(hardness)
        keys.append((str(row["scene_id"]), int(row["frame_index"])))
    return feature_frames, label_frames, hardness_frames, keys

