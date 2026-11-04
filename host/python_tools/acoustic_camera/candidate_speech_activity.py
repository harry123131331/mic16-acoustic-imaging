"""Interpretable candidate-beam speech-activity gate for the MIC16 mainline.

This module is deliberately downstream of spatial candidate extraction.  It
does not decide *where* a source is; it tests whether the mono beam steered to
an already proposed azimuth/elevation contains speech-like activity.  The
implementation is geometry-compatible with both the legacy azimuth path and
the experimental azimuth/elevation path.

The gate is a development baseline, not a trained VAD and not an FPGA block.
Its evidence is intentionally auditable: energy above an online noise floor,
speech-band energy ratio, spectral entropy, occupied-band fraction and a
short-lag periodicity proxy.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from mic16_config import Mic16Config

from .geometry_2d import far_field_arrival_delays_samples_azel


CANDIDATE_SPEECH_ACTIVITY_VERSION = "mic16_candidate_beam_sad_v1"


@dataclass(frozen=True)
class SpeechActivityFeatures:
    log_rms_dbfs: float
    speech_band_ratio: float
    spectral_entropy: float
    occupied_band_fraction: float
    spectral_flatness: float
    periodicity: float

    def to_dict(self) -> dict[str, float]:
        return asdict(self)


@dataclass(frozen=True)
class SpeechActivityThresholds:
    minimum_energy_excess_db: float = 5.0
    minimum_speech_band_ratio: float = 0.05
    minimum_spectral_entropy: float = 0.10
    maximum_spectral_entropy: float = 0.80
    minimum_occupied_band_fraction: float = 0.015
    maximum_periodicity: float = 0.995
    minimum_score: float = 0.25

    def __post_init__(self) -> None:
        # 只有实验事前明确登记时才允许负能量裕量；
        # 这支持空间证据强、低信噪比的候选波束，
        # 其独立背景片段的均方根可能略高于当前观测，
        # 默认仍为+5dB。
        if float(self.minimum_energy_excess_db) < -20.0:
            raise ValueError("minimum_energy_excess_db must be >= -20 dB")
        for name in (
            "minimum_speech_band_ratio",
            "minimum_spectral_entropy",
            "maximum_spectral_entropy",
            "minimum_occupied_band_fraction",
            "maximum_periodicity",
            "minimum_score",
        ):
            value = float(getattr(self, name))
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} must be in [0,1]")
        if float(self.minimum_spectral_entropy) >= float(
            self.maximum_spectral_entropy
        ):
            raise ValueError("spectral entropy bounds must be ordered")

    def to_dict(self) -> dict[str, float]:
        return asdict(self)


@dataclass(frozen=True)
class SpeechActivityDecision:
    active: bool
    score: float
    energy_excess_db: float
    noise_floor_dbfs: float
    reason: str
    features: SpeechActivityFeatures

    def to_dict(self) -> dict[str, object]:
        payload = asdict(self)
        payload["features"] = self.features.to_dict()
        return payload


def delay_and_sum_candidate_azel(
    pcm: np.ndarray,
    azimuth_deg: float,
    elevation_deg: float | None = None,
    cfg: Mic16Config = Mic16Config(),
) -> np.ndarray:
    """Form a far-field candidate beam without changing sample rate.

    ``elevation_deg=None`` is the one-dimensional compatibility mode and is
    equivalent to zero elevation in the horizontal array plane.
    """

    values = np.asarray(pcm, dtype=np.float32)
    if values.ndim == 3:
        values = values.reshape(-1, values.shape[-1])
    if values.ndim != 2 or values.shape[1] != int(cfg.channels):
        raise ValueError(
            f"pcm must have shape [samples,{cfg.channels}] or [frames,samples,{cfg.channels}], "
            f"got {values.shape}"
        )
    if values.shape[0] < 32:
        raise ValueError("pcm is too short for candidate beamforming")
    elevation = 0.0 if elevation_deg is None else float(elevation_deg)
    delays = far_field_arrival_delays_samples_azel(
        float(azimuth_deg), elevation, cfg
    ).astype(np.float64)
    axis = np.arange(values.shape[0], dtype=np.float64)
    aligned = np.empty_like(values, dtype=np.float32)
    for channel in range(int(cfg.channels)):
        # 到达延迟d通过采样x[n+d]补偿。
        aligned[:, channel] = np.interp(
            axis + float(delays[channel]),
            axis,
            values[:, channel],
            left=0.0,
            right=0.0,
        )
    margin = max(2, int(np.ceil(np.max(np.abs(delays)))) + 2)
    if aligned.shape[0] <= 2 * margin:
        raise ValueError("pcm is too short after beamformer edge cropping")
    return np.mean(aligned[margin:-margin], axis=1).astype(np.float32)


def _periodicity(values: np.ndarray, sample_rate: float) -> float:
    centered = np.asarray(values, dtype=np.float64) - float(np.mean(values))
    energy = float(np.dot(centered, centered))
    if energy <= 1e-12:
        return 0.0
    minimum_lag = max(1, int(np.floor(float(sample_rate) / 400.0)))
    maximum_lag = min(
        centered.size - 2,
        int(np.ceil(float(sample_rate) / 70.0)),
    )
    if maximum_lag <= minimum_lag:
        return 0.0
    correlations = []
    for lag in range(minimum_lag, maximum_lag + 1):
        left = centered[:-lag]
        right = centered[lag:]
        denominator = float(np.linalg.norm(left) * np.linalg.norm(right))
        correlations.append(0.0 if denominator <= 1e-12 else float(np.dot(left, right) / denominator))
    return float(np.clip(max(correlations, default=0.0), 0.0, 1.0))


def extract_speech_activity_features(
    beam: np.ndarray,
    sample_rate: float,
    *,
    speech_band_hz: tuple[float, float] = (300.0, 8000.0),
) -> SpeechActivityFeatures:
    """Extract deterministic, scale-aware features from one mono beam frame."""

    values = np.asarray(beam, dtype=np.float64).reshape(-1)
    if values.size < 64:
        raise ValueError("beam frame must contain at least 64 samples")
    if not np.all(np.isfinite(values)):
        raise ValueError("beam frame must be finite")
    centered = values - float(np.mean(values))
    rms = float(np.sqrt(np.mean(np.square(centered)) + 1e-20))
    log_rms = float(20.0 * np.log10(max(rms, 1e-10)))

    windowed = centered * np.hanning(centered.size)
    power = np.abs(np.fft.rfft(windowed)) ** 2
    frequencies = np.fft.rfftfreq(centered.size, d=1.0 / float(sample_rate))
    positive = frequencies >= 80.0
    speech = (frequencies >= float(speech_band_hz[0])) & (
        frequencies <= min(float(speech_band_hz[1]), 0.49 * float(sample_rate))
    )
    total_power = float(np.sum(power[positive]) + 1e-20)
    speech_power = np.asarray(power[speech], dtype=np.float64)
    speech_total = float(np.sum(speech_power) + 1e-20)
    speech_ratio = float(np.clip(speech_total / total_power, 0.0, 1.0))

    probabilities = speech_power / speech_total
    if speech_power.size <= 1:
        entropy = 0.0
    else:
        entropy = float(
            -np.sum(probabilities * np.log(probabilities + 1e-20))
            / np.log(float(speech_power.size))
        )
    peak = float(np.max(speech_power)) if speech_power.size else 0.0
    occupied = float(
        np.mean(speech_power >= peak * (10.0 ** (-25.0 / 10.0)))
    ) if peak > 0.0 else 0.0
    arithmetic = float(np.mean(speech_power) + 1e-20)
    geometric = float(np.exp(np.mean(np.log(speech_power + 1e-20))))
    flatness = float(np.clip(geometric / arithmetic, 0.0, 1.0))

    return SpeechActivityFeatures(
        log_rms_dbfs=log_rms,
        speech_band_ratio=speech_ratio,
        spectral_entropy=float(np.clip(entropy, 0.0, 1.0)),
        occupied_band_fraction=float(np.clip(occupied, 0.0, 1.0)),
        spectral_flatness=flatness,
        periodicity=_periodicity(centered, float(sample_rate)),
    )


def speech_activity_score(
    features: SpeechActivityFeatures,
    noise_floor_dbfs: float,
    thresholds: SpeechActivityThresholds = SpeechActivityThresholds(),
) -> tuple[float, float]:
    """Return bounded score and energy excess relative to the noise floor."""

    excess = float(features.log_rms_dbfs - float(noise_floor_dbfs))
    energy_term = float(
        np.clip(
            (excess - float(thresholds.minimum_energy_excess_db)) / 12.0,
            0.0,
            1.0,
        )
    )
    band_term = float(
        np.clip(
            (features.speech_band_ratio - thresholds.minimum_speech_band_ratio)
            / max(1.0 - thresholds.minimum_speech_band_ratio, 1e-6),
            0.0,
            1.0,
        )
    )
    occupancy_term = float(
        np.clip(
            (features.occupied_band_fraction - thresholds.minimum_occupied_band_fraction)
            / max(0.45 - thresholds.minimum_occupied_band_fraction, 1e-6),
            0.0,
            1.0,
        )
    )
    entropy_center = 0.5 * (
        thresholds.minimum_spectral_entropy + thresholds.maximum_spectral_entropy
    )
    entropy_half_width = 0.5 * (
        thresholds.maximum_spectral_entropy - thresholds.minimum_spectral_entropy
    )
    entropy_term = float(
        np.clip(
            1.0 - abs(features.spectral_entropy - entropy_center) / max(entropy_half_width, 1e-6),
            0.0,
            1.0,
        )
    )
    score = (
        0.50 * energy_term
        + 0.20 * band_term
        + 0.20 * occupancy_term
        + 0.10 * entropy_term
    )
    return float(np.clip(score, 0.0, 1.0)), excess


def decide_speech_activity(
    features: SpeechActivityFeatures,
    noise_floor_dbfs: float,
    thresholds: SpeechActivityThresholds = SpeechActivityThresholds(),
) -> SpeechActivityDecision:
    """Apply ordered, human-readable candidate-beam rejection rules."""

    score, excess = speech_activity_score(features, noise_floor_dbfs, thresholds)
    if excess < float(thresholds.minimum_energy_excess_db):
        reason = "REJECT_BELOW_NOISE_MARGIN"
    elif features.speech_band_ratio < float(thresholds.minimum_speech_band_ratio):
        reason = "REJECT_OUTSIDE_SPEECH_BAND"
    elif features.occupied_band_fraction < float(
        thresholds.minimum_occupied_band_fraction
    ):
        reason = "REJECT_NARROWBAND_TONAL"
    elif features.spectral_entropy < float(thresholds.minimum_spectral_entropy):
        reason = "REJECT_LOW_ENTROPY_TONAL"
    elif features.spectral_entropy > float(thresholds.maximum_spectral_entropy):
        reason = "REJECT_HIGH_ENTROPY_DIFFUSE"
    elif features.periodicity > float(thresholds.maximum_periodicity):
        reason = "REJECT_STATIONARY_PERIODIC"
    elif score < float(thresholds.minimum_score):
        reason = "REJECT_LOW_SPEECH_ACTIVITY_SCORE"
    else:
        reason = "ACCEPT_CANDIDATE_SPEECH_ACTIVITY"
    return SpeechActivityDecision(
        active=reason == "ACCEPT_CANDIDATE_SPEECH_ACTIVITY",
        score=score,
        energy_excess_db=excess,
        noise_floor_dbfs=float(noise_floor_dbfs),
        reason=reason,
        features=features,
    )


class CandidateSpeechActivityGate:
    """Causal noise-floor wrapper around the stateless feature gate."""

    def __init__(
        self,
        thresholds: SpeechActivityThresholds = SpeechActivityThresholds(),
        *,
        initial_noise_floor_dbfs: float | None = None,
        rejected_update_alpha: float = 0.08,
    ) -> None:
        if not 0.0 < float(rejected_update_alpha) <= 1.0:
            raise ValueError("rejected_update_alpha must be in (0,1]")
        self.thresholds = thresholds
        self.noise_floor_dbfs = (
            None
            if initial_noise_floor_dbfs is None
            else float(initial_noise_floor_dbfs)
        )
        self.rejected_update_alpha = float(rejected_update_alpha)

    def calibrate(self, noise_frames: np.ndarray, sample_rate: float) -> float:
        values = np.asarray(noise_frames, dtype=np.float32)
        if values.ndim == 1:
            values = values.reshape(1, -1)
        if values.ndim != 2 or values.shape[0] < 1:
            raise ValueError("noise_frames must have shape [frames,samples]")
        levels = [
            extract_speech_activity_features(frame, sample_rate).log_rms_dbfs
            for frame in values
        ]
        self.noise_floor_dbfs = float(np.median(levels))
        return self.noise_floor_dbfs

    def evaluate(
        self,
        beam_frame: np.ndarray,
        sample_rate: float,
        *,
        update_noise_floor: bool = True,
    ) -> SpeechActivityDecision:
        features = extract_speech_activity_features(beam_frame, sample_rate)
        if self.noise_floor_dbfs is None:
            self.noise_floor_dbfs = float(features.log_rms_dbfs)
            return SpeechActivityDecision(
                active=False,
                score=0.0,
                energy_excess_db=0.0,
                noise_floor_dbfs=float(self.noise_floor_dbfs),
                reason="CALIBRATING_NOISE_FLOOR",
                features=features,
            )
        decision = decide_speech_activity(
            features, float(self.noise_floor_dbfs), self.thresholds
        )
        if update_noise_floor and not decision.active:
            alpha = self.rejected_update_alpha
            # 不能让孤立高能拒绝样本立刻抬高噪声底，
            # 每次更新的观测增量上限为+3dB。
            observation = min(
                float(features.log_rms_dbfs), float(self.noise_floor_dbfs) + 3.0
            )
            self.noise_floor_dbfs = float(
                (1.0 - alpha) * float(self.noise_floor_dbfs)
                + alpha * observation
            )
        return decision
