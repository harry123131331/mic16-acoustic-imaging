"""Probabilistic candidate-beam speech-presence evidence for MIC16.

This module is deliberately placed after the frozen B6C spatial candidate
classifier and before B7 temporal confirmation.  It estimates *speech
presence*, not source direction.  The statistical evidence follows the
classical Ephraim--Malah likelihood-ratio form on a candidate beam, using a
candidate-steered calibration beam as the noise power estimate.

The implementation stays deterministic and auditable: all features, model
normalization constants and coefficients are serializable.  It is a software
development block and is not yet an FPGA implementation.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from .candidate_speech_activity import extract_speech_activity_features


PROBABILISTIC_SPEECH_PRESENCE_VERSION = "mic16_candidate_b6s_probability_v1"

SPEECH_PRESENCE_FEATURE_NAMES = (
    "mean_log_likelihood_ratio",
    "power_weighted_speech_presence_probability",
    "median_speech_presence_probability",
    "active_bin_fraction",
    "positive_snr_bin_fraction",
    "mean_aposteriori_snr_db",
    "energy_excess_db",
    "spectral_flux",
    "speech_band_ratio",
    "spectral_entropy",
    "occupied_band_fraction",
    "spectral_flatness",
    "periodicity",
)


@dataclass(frozen=True)
class SpeechPresenceTrainingConfig:
    l2_strength: float = 0.10
    learning_rate: float = 0.06
    iterations: int = 2200

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
class SpeechPresenceModel:
    feature_names: tuple[str, ...]
    means: tuple[float, ...]
    scales: tuple[float, ...]
    weights: tuple[float, ...]
    intercept: float
    training_config: SpeechPresenceTrainingConfig

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
            "version": PROBABILISTIC_SPEECH_PRESENCE_VERSION,
            "feature_names": list(self.feature_names),
            "means": list(self.means),
            "scales": list(self.scales),
            "weights": list(self.weights),
            "intercept": float(self.intercept),
            "training_config": self.training_config.to_dict(),
        }

    @classmethod
    def from_dict(cls, payload: dict[str, object]) -> "SpeechPresenceModel":
        if payload.get("version") != PROBABILISTIC_SPEECH_PRESENCE_VERSION:
            raise ValueError("unsupported speech-presence model version")
        return cls(
            tuple(str(value) for value in payload["feature_names"]),
            tuple(float(value) for value in payload["means"]),
            tuple(float(value) for value in payload["scales"]),
            tuple(float(value) for value in payload["weights"]),
            float(payload["intercept"]),
            SpeechPresenceTrainingConfig(**payload["training_config"]),
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


def _power_spectrum(values: np.ndarray) -> np.ndarray:
    frame = np.asarray(values, dtype=np.float64).reshape(-1)
    if frame.size < 64 or not np.all(np.isfinite(frame)):
        raise ValueError("beam frame must contain at least 64 finite samples")
    centered = frame - float(np.mean(frame))
    window = np.hanning(centered.size)
    normalization = max(float(np.sum(window * window)), 1e-12)
    return (np.abs(np.fft.rfft(centered * window)) ** 2 / normalization).astype(
        np.float64
    )


def estimate_noise_power_spectrum(calibration_beams: np.ndarray) -> np.ndarray:
    """Return a robust median noise PSD from candidate-steered beams."""

    values = np.asarray(calibration_beams, dtype=np.float64)
    if values.ndim == 1:
        values = values.reshape(1, -1)
    if values.ndim != 2 or values.shape[0] < 1:
        raise ValueError("calibration_beams must have shape [frame,sample]")
    spectra = np.stack([_power_spectrum(frame) for frame in values])
    noise = np.median(spectra, axis=0)
    positive = noise[noise > 0.0]
    floor = max(float(np.median(positive)) * 1e-6 if positive.size else 0.0, 1e-20)
    return np.maximum(noise, floor).astype(np.float64)


def extract_speech_presence_features(
    beam: np.ndarray,
    sample_rate: float,
    noise_power_spectrum: np.ndarray,
    *,
    previous_beam: np.ndarray | None = None,
    speech_band_hz: tuple[float, float] = (300.0, 8000.0),
) -> np.ndarray:
    """Extract likelihood-ratio and complementary speech-domain features."""

    values = np.asarray(beam, dtype=np.float64).reshape(-1)
    power = _power_spectrum(values)
    noise = np.asarray(noise_power_spectrum, dtype=np.float64).reshape(-1)
    if noise.shape != power.shape or not np.all(np.isfinite(noise)) or np.any(noise <= 0.0):
        raise ValueError("noise_power_spectrum must be positive and match beam FFT")
    frequencies = np.fft.rfftfreq(values.size, d=1.0 / float(sample_rate))
    speech_mask = (frequencies >= float(speech_band_hz[0])) & (
        frequencies <= min(float(speech_band_hz[1]), 0.49 * float(sample_rate))
    )
    if not np.any(speech_mask):
        raise ValueError("speech band contains no FFT bins")
    speech_power = power[speech_mask]
    speech_noise = noise[speech_mask]
    gamma = np.clip(speech_power / speech_noise, 1e-6, 1e6)
    # 先验信噪比的最大似然估计；小下限保证
    # 似然比有定义，但不伪造强人声。
    xi = np.clip(gamma - 1.0, 10.0 ** (-15.0 / 10.0), 1e4)
    log_likelihood_ratio = -np.log1p(xi) + gamma * xi / (1.0 + xi)
    posterior = 1.0 / (1.0 + np.exp(-np.clip(log_likelihood_ratio, -40.0, 40.0)))

    magnitude = np.sqrt(np.maximum(speech_power, 0.0))
    if previous_beam is None:
        spectral_flux = 0.0
    else:
        previous_power = _power_spectrum(previous_beam)
        if previous_power.shape != power.shape:
            # 候选角度改变会使延迟求和的边缘裁剪相差
            # 少量样本，因此在归一化频率网格比较，
            # 不强制FFT长度相同。
            previous_power = np.interp(
                np.linspace(0.0, 1.0, power.size),
                np.linspace(0.0, 1.0, previous_power.size),
                previous_power,
            )
        previous_magnitude = np.sqrt(np.maximum(previous_power[speech_mask], 0.0))
        scale = max(float(np.linalg.norm(previous_magnitude)), 1e-12)
        spectral_flux = float(
            np.linalg.norm(np.maximum(magnitude - previous_magnitude, 0.0)) / scale
        )

    energy_excess_db = float(
        10.0
        * np.log10(
            max(float(np.sum(speech_power)), 1e-20)
            / max(float(np.sum(speech_noise)), 1e-20)
        )
    )
    activity = extract_speech_activity_features(values, float(sample_rate))
    feature_vector = np.asarray(
        [
            float(np.mean(log_likelihood_ratio)),
            float(
                np.sum(posterior * speech_power)
                / max(float(np.sum(speech_power)), 1e-20)
            ),
            float(np.median(posterior)),
            float(np.mean(posterior >= 0.60)),
            float(np.mean(gamma > 1.0)),
            float(10.0 * np.log10(max(float(np.mean(gamma)), 1e-20))),
            energy_excess_db,
            spectral_flux,
            float(activity.speech_band_ratio),
            float(activity.spectral_entropy),
            float(activity.occupied_band_fraction),
            float(activity.spectral_flatness),
            float(activity.periodicity),
        ],
        dtype=np.float64,
    )
    if feature_vector.size != len(SPEECH_PRESENCE_FEATURE_NAMES) or not np.all(
        np.isfinite(feature_vector)
    ):
        raise RuntimeError("speech-presence feature schema mismatch")
    return feature_vector


def fit_speech_presence_model(
    features: np.ndarray,
    labels: np.ndarray,
    config: SpeechPresenceTrainingConfig = SpeechPresenceTrainingConfig(),
    *,
    feature_names: tuple[str, ...] = SPEECH_PRESENCE_FEATURE_NAMES,
) -> SpeechPresenceModel:
    """Fit a deterministic class-balanced L2 logistic speech model."""

    names = tuple(str(name) for name in feature_names)
    if not names or len(names) != len(set(names)):
        raise ValueError("feature_names must be non-empty and unique")
    values = _feature_matrix(features, len(names))
    target = np.asarray(labels, dtype=np.float64).reshape(-1)
    if target.size != values.shape[0] or not np.all(np.isin(target, (0.0, 1.0))):
        raise ValueError("labels must be binary and match feature rows")
    positive = int(np.count_nonzero(target == 1.0))
    negative = int(np.count_nonzero(target == 0.0))
    if positive == 0 or negative == 0:
        raise ValueError("both speech-presence classes are required")
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
        gradient = standardized.T @ residual / normalizer
        gradient += float(config.l2_strength) * weights
        intercept_gradient = float(np.sum(residual) / normalizer)
        learning_rate = float(config.learning_rate) / np.sqrt(1.0 + step / 300.0)
        weights -= learning_rate * gradient
        intercept -= learning_rate * intercept_gradient
    return SpeechPresenceModel(
        names,
        tuple(float(value) for value in means),
        tuple(float(value) for value in scales),
        tuple(float(value) for value in weights),
        float(intercept),
        config,
    )
