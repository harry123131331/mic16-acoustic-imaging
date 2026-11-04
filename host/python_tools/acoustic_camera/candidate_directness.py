"""Candidate-steered directness proxy for the planar MIC16 array.

This module is an isolated PC research component for Phase 2C.  It does not
claim to estimate a calibrated physical direct-to-diffuse ratio.  Instead it
phase-aligns the sixteen microphone spectra for an already proposed Az/El
candidate and measures how much multichannel power becomes coherent.  A
direct plane wave should align over many frequency bins, while a multi-
direction diffuse field or strong off-direction multipath should not.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from mic16_channel_map import apply_channel_map, default_channel_map, validate_channel_map
from mic16_config import Mic16Config

from .cross_band_consistency import DEFAULT_SPEECH_SUBBANDS, SubbandSpec
from .geometry_2d import far_field_arrival_delays_samples_azel


CANDIDATE_DIRECTNESS_VERSION = "mic16_candidate_steered_directness_proxy_v1"


@dataclass(frozen=True)
class DirectnessThresholds:
    minimum_weighted_coherence: float = 0.55
    minimum_median_coherence: float = 0.35
    minimum_coherent_bin_fraction: float = 0.35
    coherent_bin_threshold: float = 0.50
    minimum_relative_band_energy: float = 0.02
    minimum_active_bands: int = 2
    minimum_band_coherence: float = 0.35

    def __post_init__(self) -> None:
        for name in (
            "minimum_weighted_coherence",
            "minimum_median_coherence",
            "minimum_coherent_bin_fraction",
            "coherent_bin_threshold",
            "minimum_relative_band_energy",
            "minimum_band_coherence",
        ):
            value = float(getattr(self, name))
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} must be in [0,1]")
        if int(self.minimum_active_bands) < 1:
            raise ValueError("minimum_active_bands must be positive")

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class BandDirectness:
    band_name: str
    relative_energy: float
    active: bool
    weighted_coherence: float
    median_coherence: float
    coherent_bin_fraction: float

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class DirectnessFeatures:
    candidate_azimuth_deg: float
    candidate_elevation_deg: float
    weighted_coherence: float
    median_coherence: float
    lower_quartile_coherence: float
    coherent_bin_fraction: float
    diffuseness_proxy: float
    active_band_count: int
    minimum_active_band_coherence: float
    bands: tuple[BandDirectness, ...]

    def to_dict(self) -> dict[str, object]:
        payload = asdict(self)
        payload["bands"] = [item.to_dict() for item in self.bands]
        return payload


@dataclass(frozen=True)
class DirectnessDecision:
    accepted: bool
    reason: str
    features: DirectnessFeatures

    def to_dict(self) -> dict[str, object]:
        payload = asdict(self)
        payload["features"] = self.features.to_dict()
        return payload


def _weighted_mean(values: np.ndarray, weights: np.ndarray) -> float:
    denominator = float(np.sum(weights))
    if denominator <= 1e-20:
        return 0.0
    return float(np.sum(values * weights) / denominator)


def _spectral_coherence_for_candidate(
    frame: np.ndarray,
    candidate_azimuth_deg: float,
    candidate_elevation_deg: float,
    cfg: Mic16Config,
    channel_map: np.ndarray | None,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    values = np.asarray(frame, dtype=np.float32)
    expected = (int(cfg.frame_samples), int(cfg.channels))
    if values.shape != expected:
        raise ValueError(f"frame must have shape {expected}, got {values.shape}")
    if not np.all(np.isfinite(values)):
        raise ValueError("frame must be finite")
    cmap = (
        default_channel_map(cfg)
        if channel_map is None
        else validate_channel_map(channel_map, cfg)
    )
    logical = apply_channel_map(values, cmap, cfg).astype(np.float64)
    logical -= np.mean(logical, axis=0, keepdims=True)
    logical *= np.hanning(logical.shape[0]).reshape(-1, 1)
    spectra = np.fft.rfft(logical, n=int(cfg.fft_size), axis=0)
    frequencies = np.fft.rfftfreq(int(cfg.fft_size), d=1.0 / float(cfg.sample_rate))
    delays = far_field_arrival_delays_samples_azel(
        float(candidate_azimuth_deg), float(candidate_elevation_deg), cfg
    ).astype(np.float64)
    steering = np.exp(
        2j
        * np.pi
        * frequencies.reshape(-1, 1)
        * delays.reshape(1, -1)
        / float(cfg.sample_rate)
    )
    aligned = spectra * steering
    total_power = np.mean(np.abs(aligned) ** 2, axis=1)
    coherent_power = np.abs(np.mean(aligned, axis=1)) ** 2
    coherence = np.divide(
        coherent_power,
        total_power,
        out=np.zeros_like(coherent_power, dtype=np.float64),
        where=total_power > 1e-20,
    )
    return frequencies, np.clip(coherence, 0.0, 1.0), total_power


def extract_candidate_directness(
    frame: np.ndarray,
    candidate_azimuth_deg: float,
    candidate_elevation_deg: float,
    cfg: Mic16Config = Mic16Config(),
    *,
    bands: tuple[SubbandSpec, ...] = DEFAULT_SPEECH_SUBBANDS,
    channel_map: np.ndarray | None = None,
    coherent_bin_threshold: float = 0.50,
    minimum_relative_band_energy: float = 0.02,
) -> DirectnessFeatures:
    """Extract a bounded candidate-steered coherence/directness proxy."""

    if not 0.0 <= float(coherent_bin_threshold) <= 1.0:
        raise ValueError("coherent_bin_threshold must be in [0,1]")
    if not 0.0 <= float(minimum_relative_band_energy) <= 1.0:
        raise ValueError("minimum_relative_band_energy must be in [0,1]")
    band_specs = tuple(bands)
    if len(band_specs) < 2:
        raise ValueError("at least two bands are required")
    frequencies, coherence, power = _spectral_coherence_for_candidate(
        frame,
        candidate_azimuth_deg,
        candidate_elevation_deg,
        cfg,
        channel_map,
    )
    analysis_mask = (frequencies >= float(band_specs[0].low_hz)) & (
        frequencies < float(band_specs[-1].high_hz)
    )
    if not np.any(analysis_mask):
        raise ValueError("analysis bands contain no FFT bins")
    analysis_power = power[analysis_mask]
    # 限制孤立强频点对宽带直达性评分的支配。
    weight_ceiling = float(np.percentile(analysis_power, 90.0))
    weights = np.minimum(analysis_power, max(weight_ceiling, 1e-20))
    analysis_coherence = coherence[analysis_mask]
    weighted = _weighted_mean(analysis_coherence, weights)
    median = float(np.median(analysis_coherence))
    lower_quartile = float(np.percentile(analysis_coherence, 25.0))
    coherent_fraction = float(
        np.mean(analysis_coherence >= float(coherent_bin_threshold))
    )

    raw_band_energy: list[float] = []
    masks: list[np.ndarray] = []
    for band in band_specs:
        mask = (frequencies >= float(band.low_hz)) & (
            frequencies < float(band.high_hz)
        )
        if not np.any(mask):
            raise ValueError(f"band {band.name} contains no FFT bins")
        masks.append(mask)
        raw_band_energy.append(float(np.sum(power[mask])))
    maximum_band_energy = max(raw_band_energy, default=0.0)
    relative_energies = [
        0.0 if maximum_band_energy <= 1e-20 else item / maximum_band_energy
        for item in raw_band_energy
    ]
    band_features: list[BandDirectness] = []
    for band, mask, relative_energy in zip(
        band_specs, masks, relative_energies, strict=True
    ):
        band_power = power[mask]
        band_ceiling = float(np.percentile(band_power, 90.0))
        band_weights = np.minimum(band_power, max(band_ceiling, 1e-20))
        band_coherence = coherence[mask]
        band_features.append(
            BandDirectness(
                band_name=band.name,
                relative_energy=float(relative_energy),
                active=float(relative_energy) >= float(minimum_relative_band_energy),
                weighted_coherence=_weighted_mean(band_coherence, band_weights),
                median_coherence=float(np.median(band_coherence)),
                coherent_bin_fraction=float(
                    np.mean(band_coherence >= float(coherent_bin_threshold))
                ),
            )
        )
    active = [item for item in band_features if item.active]
    minimum_active = (
        0.0 if not active else min(item.weighted_coherence for item in active)
    )
    return DirectnessFeatures(
        candidate_azimuth_deg=float(candidate_azimuth_deg),
        candidate_elevation_deg=float(candidate_elevation_deg),
        weighted_coherence=float(weighted),
        median_coherence=median,
        lower_quartile_coherence=lower_quartile,
        coherent_bin_fraction=coherent_fraction,
        diffuseness_proxy=float(1.0 - weighted),
        active_band_count=len(active),
        minimum_active_band_coherence=float(minimum_active),
        bands=tuple(band_features),
    )


def evaluate_candidate_directness(
    frame: np.ndarray,
    candidate_azimuth_deg: float,
    candidate_elevation_deg: float,
    cfg: Mic16Config = Mic16Config(),
    *,
    thresholds: DirectnessThresholds = DirectnessThresholds(),
    bands: tuple[SubbandSpec, ...] = DEFAULT_SPEECH_SUBBANDS,
    channel_map: np.ndarray | None = None,
) -> DirectnessDecision:
    """Apply an ordered, explainable gate to the directness proxy."""

    features = extract_candidate_directness(
        frame,
        candidate_azimuth_deg,
        candidate_elevation_deg,
        cfg,
        bands=bands,
        channel_map=channel_map,
        coherent_bin_threshold=float(thresholds.coherent_bin_threshold),
        minimum_relative_band_energy=float(thresholds.minimum_relative_band_energy),
    )
    return apply_directness_gate(features, thresholds)


def apply_directness_gate(
    features: DirectnessFeatures,
    thresholds: DirectnessThresholds = DirectnessThresholds(),
) -> DirectnessDecision:
    """Classify precomputed features without recomputing the frame spectrum."""

    if features.active_band_count < int(thresholds.minimum_active_bands):
        reason = "REJECT_TOO_FEW_DIRECTNESS_BANDS"
    elif features.weighted_coherence < float(
        thresholds.minimum_weighted_coherence
    ):
        reason = "REJECT_LOW_STEERED_COHERENCE"
    elif features.median_coherence < float(thresholds.minimum_median_coherence):
        reason = "REJECT_LOW_MEDIAN_COHERENCE"
    elif features.coherent_bin_fraction < float(
        thresholds.minimum_coherent_bin_fraction
    ):
        reason = "REJECT_LOW_COHERENT_BIN_FRACTION"
    elif features.minimum_active_band_coherence < float(
        thresholds.minimum_band_coherence
    ):
        reason = "REJECT_DIFFUSE_ACTIVE_BAND"
    else:
        reason = "ACCEPT_DIRECTNESS_PROXY"
    return DirectnessDecision(
        accepted=reason == "ACCEPT_DIRECTNESS_PROXY",
        reason=reason,
        features=features,
    )
