"""Causal frame features and within-frame pairwise direction ranking.

The module is the DEV-4 research extension of the two-stage valid-source
pipeline.  Stage 1 receives only present and past aggregate evidence.  Stage 2
learns candidate ordering from true-candidate versus hard-negative pairs,
rather than treating candidates from the same frame as independent examples.

This is PC research code and does not change the formal FPGA ABI.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from .two_stage_valid_source import FRAME_ACTIVITY_FEATURE_NAMES


CAUSAL_PAIRWISE_VERSION = "mic16_causal_pairwise_valid_source_v1"

CAUSAL_ACTIVITY_SOURCE_FEATURE_NAMES = (
    "b6c_probability_max",
    "b6s_max_power_weighted_speech_presence_probability",
    "b6s_max_active_bin_fraction",
    "b6s_max_mean_aposteriori_snr_db",
    "b6s_max_energy_excess_db",
    "b6s_max_spectral_flux",
    "b6s_median_mean_log_likelihood_ratio",
    "b6s_median_power_weighted_speech_presence_probability",
    "b6s_median_energy_excess_db",
)

CAUSAL_ACTIVITY_FEATURE_NAMES = (
    FRAME_ACTIVITY_FEATURE_NAMES
    + tuple(f"delta1_{name}" for name in CAUSAL_ACTIVITY_SOURCE_FEATURE_NAMES)
    + tuple(f"prior3_drop_{name}" for name in CAUSAL_ACTIVITY_SOURCE_FEATURE_NAMES)
)


def causal_activity_feature_sequence(base_features: np.ndarray) -> np.ndarray:
    """Append strictly causal first differences and prior-three peak drops."""

    values = np.asarray(base_features, dtype=np.float64)
    if values.ndim != 2 or values.shape[1] != len(FRAME_ACTIVITY_FEATURE_NAMES):
        raise ValueError(
            f"base_features must have shape [frame,{len(FRAME_ACTIVITY_FEATURE_NAMES)}]"
        )
    if not np.all(np.isfinite(values)):
        raise ValueError("base_features must be finite")
    indices = np.asarray(
        [FRAME_ACTIVITY_FEATURE_NAMES.index(name) for name in CAUSAL_ACTIVITY_SOURCE_FEATURE_NAMES],
        dtype=np.int64,
    )
    selected = values[:, indices]
    delta = np.zeros_like(selected)
    prior_drop = np.zeros_like(selected)
    for frame_index in range(values.shape[0]):
        if frame_index == 0:
            continue
        delta[frame_index] = selected[frame_index] - selected[frame_index - 1]
        start = max(0, frame_index - 3)
        prior_peak = np.max(selected[start:frame_index], axis=0)
        prior_drop[frame_index] = selected[frame_index] - prior_peak
    output = np.concatenate((values, delta, prior_drop), axis=1)
    if output.shape[1] != len(CAUSAL_ACTIVITY_FEATURE_NAMES):
        raise RuntimeError("causal activity feature schema mismatch")
    return output


@dataclass(frozen=True)
class PairwiseRankerTrainingConfig:
    l2_strength: float = 0.10
    learning_rate: float = 0.05
    iterations: int = 2600
    hard_negative_count: int = 4

    def __post_init__(self) -> None:
        if float(self.l2_strength) < 0.0:
            raise ValueError("l2_strength must be non-negative")
        if float(self.learning_rate) <= 0.0 or int(self.iterations) < 1:
            raise ValueError("learning rate and iterations must be positive")
        if int(self.hard_negative_count) < 1:
            raise ValueError("hard_negative_count must be positive")

    def to_dict(self) -> dict[str, float | int]:
        return asdict(self)


@dataclass(frozen=True)
class PairwiseCandidateRanker:
    feature_names: tuple[str, ...]
    means: tuple[float, ...]
    scales: tuple[float, ...]
    weights: tuple[float, ...]
    training_config: PairwiseRankerTrainingConfig
    training_pair_count: int
    training_frame_count: int

    def __post_init__(self) -> None:
        size = len(self.feature_names)
        if size == 0 or not (
            len(self.means) == len(self.scales) == len(self.weights) == size
        ):
            raise ValueError("ranker vectors must match feature_names")
        if any(float(value) <= 0.0 for value in self.scales):
            raise ValueError("ranker scales must be positive")
        if int(self.training_pair_count) < 1 or int(self.training_frame_count) < 1:
            raise ValueError("ranker training counts must be positive")

    def decision_function(self, features: np.ndarray) -> np.ndarray:
        values = _feature_matrix(features, len(self.feature_names))
        standardized = (values - np.asarray(self.means)) / np.asarray(self.scales)
        return standardized @ np.asarray(self.weights)

    def predict_probabilities(self, features: np.ndarray) -> np.ndarray:
        scores = self.decision_function(features)
        shifted = np.clip(scores - float(np.max(scores)), -40.0, 0.0)
        exponent = np.exp(shifted)
        return exponent / max(float(np.sum(exponent)), 1e-20)

    def to_dict(self) -> dict[str, object]:
        return {
            "version": CAUSAL_PAIRWISE_VERSION,
            "feature_names": list(self.feature_names),
            "means": list(self.means),
            "scales": list(self.scales),
            "weights": list(self.weights),
            "training_config": self.training_config.to_dict(),
            "training_pair_count": int(self.training_pair_count),
            "training_frame_count": int(self.training_frame_count),
        }

    @classmethod
    def from_dict(cls, payload: dict[str, object]) -> "PairwiseCandidateRanker":
        if payload.get("version") != CAUSAL_PAIRWISE_VERSION:
            raise ValueError("unsupported pairwise-ranker version")
        return cls(
            tuple(str(value) for value in payload["feature_names"]),
            tuple(float(value) for value in payload["means"]),
            tuple(float(value) for value in payload["scales"]),
            tuple(float(value) for value in payload["weights"]),
            PairwiseRankerTrainingConfig(**payload["training_config"]),
            int(payload["training_pair_count"]),
            int(payload["training_frame_count"]),
        )


def _feature_matrix(features: np.ndarray, feature_count: int) -> np.ndarray:
    values = np.asarray(features, dtype=np.float64)
    if values.ndim == 1:
        values = values.reshape(1, -1)
    if values.ndim != 2 or values.shape[1] != feature_count:
        raise ValueError(f"features must have shape [sample,{feature_count}]")
    if not np.all(np.isfinite(values)):
        raise ValueError("features must be finite")
    return values


def fit_pairwise_candidate_ranker(
    frame_features: list[np.ndarray],
    frame_labels: list[np.ndarray],
    frame_hardness: list[np.ndarray],
    config: PairwiseRankerTrainingConfig = PairwiseRankerTrainingConfig(),
    *,
    feature_names: tuple[str, ...],
) -> PairwiseCandidateRanker:
    """Fit a deterministic RankNet-style linear ranker on within-frame pairs."""

    names = tuple(str(name) for name in feature_names)
    if not names or len(names) != len(set(names)):
        raise ValueError("feature_names must be non-empty and unique")
    if not (len(frame_features) == len(frame_labels) == len(frame_hardness)):
        raise ValueError("frame inputs must have equal length")
    matrices: list[np.ndarray] = []
    normalized_labels: list[np.ndarray] = []
    normalized_hardness: list[np.ndarray] = []
    for features, labels, hardness in zip(
        frame_features, frame_labels, frame_hardness, strict=True
    ):
        matrix = _feature_matrix(features, len(names))
        target = np.asarray(labels, dtype=np.int8).reshape(-1)
        hard = np.asarray(hardness, dtype=np.float64).reshape(-1)
        if target.size != matrix.shape[0] or hard.size != matrix.shape[0]:
            raise ValueError("labels and hardness must match frame candidates")
        if not np.all(np.isin(target, (0, 1))) or not np.all(np.isfinite(hard)):
            raise ValueError("labels must be binary and hardness finite")
        if np.any(target == 1) and np.any(target == 0):
            matrices.append(matrix)
            normalized_labels.append(target)
            normalized_hardness.append(hard)
    if not matrices:
        raise ValueError("at least one frame with positive and negative candidates is required")
    all_values = np.concatenate(matrices, axis=0)
    means = np.mean(all_values, axis=0)
    scales = np.std(all_values, axis=0)
    scales = np.where(scales < 1e-8, 1.0, scales)
    pair_differences: list[np.ndarray] = []
    used_frames = 0
    for matrix, labels, hardness in zip(
        matrices, normalized_labels, normalized_hardness, strict=True
    ):
        standardized = (matrix - means) / scales
        positive_indices = np.flatnonzero(labels == 1)
        negative_indices = np.flatnonzero(labels == 0)
        order = negative_indices[
            np.argsort(-hardness[negative_indices], kind="stable")
        ][: int(config.hard_negative_count)]
        if not order.size:
            continue
        used_frames += 1
        for positive_index in positive_indices:
            for negative_index in order:
                pair_differences.append(
                    standardized[int(positive_index)] - standardized[int(negative_index)]
                )
    if not pair_differences:
        raise ValueError("pairwise training generated no pairs")
    differences = np.asarray(pair_differences, dtype=np.float64)
    # 成对构造对称RankNet样本，避免方向顺序使最优解偏置。
    training_x = np.concatenate((differences, -differences), axis=0)
    training_y = np.concatenate(
        (np.ones(differences.shape[0]), np.zeros(differences.shape[0]))
    )
    weights = np.zeros(training_x.shape[1], dtype=np.float64)
    for step in range(int(config.iterations)):
        logits = np.clip(training_x @ weights, -40.0, 40.0)
        probability = 1.0 / (1.0 + np.exp(-logits))
        gradient = training_x.T @ (probability - training_y) / training_x.shape[0]
        gradient += float(config.l2_strength) * weights
        learning_rate = float(config.learning_rate) / np.sqrt(1.0 + step / 300.0)
        weights -= learning_rate * gradient
    return PairwiseCandidateRanker(
        names,
        tuple(float(value) for value in means),
        tuple(float(value) for value in scales),
        tuple(float(value) for value in weights),
        config,
        int(differences.shape[0]),
        int(used_frames),
    )
