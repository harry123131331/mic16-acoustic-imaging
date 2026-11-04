"""Small deterministic logistic classifier for MIC16 candidate evidence."""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np


INTERPRETABLE_CANDIDATE_CLASSIFIER_VERSION = "mic16_candidate_logistic_v1"


FEATURE_NAMES = (
    "inverse_rank",
    "normalized_peak",
    "prominence",
    "local_contrast",
    "pair_consistency",
    "pair_support_fraction",
    "cross_consistency_ratio",
    "cross_active_fraction",
    "cross_supported_fraction",
    "cross_direction_agreement",
    "direct_weighted_coherence",
    "direct_median_coherence",
    "direct_lower_quartile_coherence",
    "direct_coherent_bin_fraction",
    "direct_minimum_band_coherence",
    "speech_activity_score",
    "speech_energy_excess_db",
    "speech_band_ratio",
    "speech_spectral_entropy",
    "speech_occupied_band_fraction",
    "speech_inverse_periodicity",
)


@dataclass(frozen=True)
class LogisticTrainingConfig:
    l2_strength: float = 0.08
    learning_rate: float = 0.08
    iterations: int = 1800

    def __post_init__(self) -> None:
        if float(self.l2_strength) < 0.0:
            raise ValueError("l2_strength must be non-negative")
        if float(self.learning_rate) <= 0.0:
            raise ValueError("learning_rate must be positive")
        if int(self.iterations) < 1:
            raise ValueError("iterations must be positive")

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class CandidateLogisticModel:
    feature_names: tuple[str, ...]
    means: tuple[float, ...]
    scales: tuple[float, ...]
    weights: tuple[float, ...]
    intercept: float
    training_config: LogisticTrainingConfig

    def __post_init__(self) -> None:
        size = len(self.feature_names)
        if size == 0 or not (
            len(self.means) == len(self.scales) == len(self.weights) == size
        ):
            raise ValueError("model vectors must match feature_names")
        if any(float(value) <= 0.0 for value in self.scales):
            raise ValueError("feature scales must be positive")

    def to_dict(self) -> dict[str, object]:
        return {
            "version": INTERPRETABLE_CANDIDATE_CLASSIFIER_VERSION,
            "feature_names": list(self.feature_names),
            "means": list(self.means),
            "scales": list(self.scales),
            "weights": list(self.weights),
            "intercept": float(self.intercept),
            "training_config": self.training_config.to_dict(),
        }

    @classmethod
    def from_dict(cls, payload: dict[str, object]) -> "CandidateLogisticModel":
        if payload.get("version") != INTERPRETABLE_CANDIDATE_CLASSIFIER_VERSION:
            raise ValueError("unsupported candidate classifier version")
        return cls(
            tuple(str(value) for value in payload["feature_names"]),
            tuple(float(value) for value in payload["means"]),
            tuple(float(value) for value in payload["scales"]),
            tuple(float(value) for value in payload["weights"]),
            float(payload["intercept"]),
            LogisticTrainingConfig(**payload["training_config"]),
        )

    def decision_function(self, features: np.ndarray) -> np.ndarray:
        values = _feature_matrix(features, len(self.feature_names))
        standardized = (values - np.asarray(self.means)) / np.asarray(self.scales)
        return standardized @ np.asarray(self.weights) + float(self.intercept)

    def predict_proba(self, features: np.ndarray) -> np.ndarray:
        logits = np.clip(self.decision_function(features), -40.0, 40.0)
        return 1.0 / (1.0 + np.exp(-logits))


def _feature_matrix(features: np.ndarray, feature_count: int) -> np.ndarray:
    values = np.asarray(features, dtype=np.float64)
    if values.ndim == 1:
        values = values.reshape(1, -1)
    if values.ndim != 2 or values.shape[1] != int(feature_count):
        raise ValueError(f"features must have shape [sample,{feature_count}]")
    if not np.all(np.isfinite(values)):
        raise ValueError("features must be finite")
    return values


def fit_candidate_logistic(
    features: np.ndarray,
    labels: np.ndarray,
    config: LogisticTrainingConfig = LogisticTrainingConfig(),
) -> CandidateLogisticModel:
    """Fit a class-balanced, L2-regularized logistic model deterministically."""

    values = _feature_matrix(features, len(FEATURE_NAMES))
    target = np.asarray(labels, dtype=np.float64).reshape(-1)
    if target.size != values.shape[0] or not np.all(np.isin(target, (0.0, 1.0))):
        raise ValueError("labels must be binary and match feature rows")
    positive = int(np.count_nonzero(target == 1.0))
    negative = int(np.count_nonzero(target == 0.0))
    if positive == 0 or negative == 0:
        raise ValueError("both candidate classes are required")
    means = np.mean(values, axis=0)
    scales = np.std(values, axis=0)
    scales = np.where(scales < 1e-8, 1.0, scales)
    standardized = (values - means) / scales
    sample_weights = np.where(
        target == 1.0,
        values.shape[0] / (2.0 * positive),
        values.shape[0] / (2.0 * negative),
    )
    weights = np.zeros(values.shape[1], dtype=np.float64)
    intercept = 0.0
    normalizer = float(np.sum(sample_weights))
    for step in range(int(config.iterations)):
        logits = np.clip(standardized @ weights + intercept, -40.0, 40.0)
        probabilities = 1.0 / (1.0 + np.exp(-logits))
        residual = (probabilities - target) * sample_weights
        weight_gradient = standardized.T @ residual / normalizer
        weight_gradient += float(config.l2_strength) * weights
        intercept_gradient = float(np.sum(residual) / normalizer)
        learning_rate = float(config.learning_rate) / np.sqrt(1.0 + step / 300.0)
        weights -= learning_rate * weight_gradient
        intercept -= learning_rate * intercept_gradient
    return CandidateLogisticModel(
        FEATURE_NAMES,
        tuple(float(value) for value in means),
        tuple(float(value) for value in scales),
        tuple(float(value) for value in weights),
        float(intercept),
        config,
    )
