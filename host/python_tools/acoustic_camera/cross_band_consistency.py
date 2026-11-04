"""Cross-band spatial consistency for MIC16 valid-source candidates.

The frozen full-band 32-pair x 41-lag frontend is not modified.  This module
is an isolated PC research branch that forms three windowed subband GCC-PHAT
sets and asks whether an already proposed spatial candidate is supported at a
similar direction in more than one energized band.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Iterable

import numpy as np

from mic16_channel_map import apply_channel_map, default_channel_map, validate_channel_map
from mic16_config import Mic16Config, compact32_pairs

from .valid_source_detection import angular_distance_deg


CROSS_BAND_CONSISTENCY_VERSION = "mic16_cross_band_candidate_consistency_v1"


@dataclass(frozen=True)
class SubbandSpec:
    name: str
    low_hz: float
    high_hz: float

    def __post_init__(self) -> None:
        if not str(self.name):
            raise ValueError("subband name must be non-empty")
        if float(self.low_hz) < 0.0 or float(self.low_hz) >= float(self.high_hz):
            raise ValueError("subband frequencies must satisfy 0 <= low < high")

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


DEFAULT_SPEECH_SUBBANDS = (
    SubbandSpec("low", 300.0, 1200.0),
    SubbandSpec("mid", 1200.0, 3000.0),
    SubbandSpec("high", 3000.0, 7000.0),
)


@dataclass(frozen=True)
class SubbandGccBundle:
    bands: tuple[SubbandSpec, ...]
    gcc_phat: np.ndarray
    energy_db: np.ndarray
    relative_energy: np.ndarray


@dataclass(frozen=True)
class CrossBandThresholds:
    minimum_relative_band_energy: float = 0.08
    minimum_active_bands: int = 2
    minimum_supported_bands: int = 2
    minimum_local_response: float = 0.55
    support_radius_deg: float = 15.0
    maximum_median_offset_deg: float = 12.0

    def __post_init__(self) -> None:
        if not 0.0 <= float(self.minimum_relative_band_energy) <= 1.0:
            raise ValueError("minimum_relative_band_energy must be in [0,1]")
        if int(self.minimum_active_bands) < 1:
            raise ValueError("minimum_active_bands must be positive")
        if int(self.minimum_supported_bands) < 1:
            raise ValueError("minimum_supported_bands must be positive")
        if not 0.0 <= float(self.minimum_local_response) <= 1.0:
            raise ValueError("minimum_local_response must be in [0,1]")
        if float(self.support_radius_deg) <= 0.0:
            raise ValueError("support_radius_deg must be positive")
        if float(self.maximum_median_offset_deg) < 0.0:
            raise ValueError("maximum_median_offset_deg must be non-negative")

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class BandCandidateSupport:
    band_name: str
    relative_energy: float
    active: bool
    dynamic_range: float
    local_peak_azimuth_deg: float | None
    local_peak_elevation_deg: float | None
    local_peak_offset_deg: float | None
    normalized_local_response: float
    supported: bool

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class CrossBandDecision:
    accepted: bool
    reason: str
    active_band_count: int
    supported_band_count: int
    consistency_ratio: float
    median_supported_offset_deg: float | None
    band_support: tuple[BandCandidateSupport, ...]

    def to_dict(self) -> dict[str, object]:
        payload = asdict(self)
        payload["band_support"] = [item.to_dict() for item in self.band_support]
        return payload


def extract_subband_gcc_phat(
    frame: np.ndarray,
    cfg: Mic16Config = Mic16Config(),
    *,
    bands: Iterable[SubbandSpec] = DEFAULT_SPEECH_SUBBANDS,
    pairs: np.ndarray | None = None,
    channel_map: np.ndarray | None = None,
) -> SubbandGccBundle:
    """Return Hann-windowed subband GCC-PHAT and auditable band energies."""

    values = np.asarray(frame, dtype=np.float32)
    if values.shape != (int(cfg.frame_samples), int(cfg.channels)):
        raise ValueError(
            f"frame must have shape ({cfg.frame_samples},{cfg.channels}), got {values.shape}"
        )
    if not np.all(np.isfinite(values)):
        raise ValueError("frame must be finite")
    band_specs = tuple(bands)
    if len(band_specs) < 2:
        raise ValueError("at least two subbands are required")
    nyquist = 0.5 * float(cfg.sample_rate)
    for band in band_specs:
        if float(band.high_hz) >= nyquist:
            raise ValueError(f"subband {band.name} reaches Nyquist")
    pair_array = compact32_pairs(cfg) if pairs is None else np.asarray(pairs, dtype=np.int64)
    if pair_array.ndim != 2 or pair_array.shape[1] != 2:
        raise ValueError("pairs must have shape [pair,2]")
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
    lag_indices = (-np.arange(-int(cfg.max_lag), int(cfg.max_lag) + 1)) % int(cfg.fft_size)
    all_curves = np.empty(
        (len(band_specs), pair_array.shape[0], 2 * int(cfg.max_lag) + 1),
        dtype=np.float32,
    )
    energies = np.empty(len(band_specs), dtype=np.float64)
    for band_index, band in enumerate(band_specs):
        mask = (frequencies >= float(band.low_hz)) & (
            frequencies < float(band.high_hz)
        )
        energies[band_index] = float(np.mean(np.abs(spectra[mask]) ** 2))
        for pair_index, (channel_a, channel_b) in enumerate(pair_array):
            cross = spectra[:, int(channel_a)] * np.conj(spectra[:, int(channel_b)])
            magnitude = np.abs(cross)
            phat = np.zeros_like(cross)
            valid = mask & (magnitude > float(cfg.phat_epsilon))
            phat[valid] = cross[valid] / (magnitude[valid] + float(cfg.phat_epsilon))
            correlation = np.fft.irfft(phat, n=int(cfg.fft_size))
            all_curves[band_index, pair_index] = correlation[lag_indices].astype(np.float32)
    maximum_energy = float(np.max(energies))
    relative = (
        np.zeros_like(energies)
        if maximum_energy <= 1e-20
        else energies / maximum_energy
    )
    energy_db = 10.0 * np.log10(np.maximum(energies, 1e-20))
    return SubbandGccBundle(
        bands=band_specs,
        gcc_phat=all_curves,
        energy_db=energy_db.astype(np.float32),
        relative_energy=relative.astype(np.float32),
    )


def subband_spatial_responses(
    bundle: SubbandGccBundle,
    lag_table: np.ndarray,
) -> np.ndarray:
    """Sample every subband pair curve on a 1-D or 2-D lag table."""

    table = np.asarray(lag_table, dtype=np.float32)
    curves = np.asarray(bundle.gcc_phat, dtype=np.float32)
    if table.ndim not in (2, 3):
        raise ValueError("lag_table must have shape [azimuth,pair] or [elevation,azimuth,pair]")
    if curves.ndim != 3 or curves.shape[2] % 2 != 1:
        raise ValueError("bundle GCC array must have shape [band,pair,odd_lag]")
    if table.shape[-1] != curves.shape[1]:
        raise ValueError("lag_table pair count does not match GCC bundle")
    maximum_lag = (curves.shape[2] - 1) // 2
    positions = np.clip(table + float(maximum_lag), 0.0, float(2 * maximum_lag))
    lower = np.floor(positions).astype(np.int64)
    upper = np.minimum(lower + 1, 2 * maximum_lag).astype(np.int64)
    fraction = positions - lower.astype(np.float32)
    pair_indices = np.arange(curves.shape[1], dtype=np.int64)
    output = []
    for band_curves in curves:
        lower_values = band_curves[pair_indices, lower]
        upper_values = band_curves[pair_indices, upper]
        sampled = lower_values * (1.0 - fraction) + upper_values * fraction
        output.append(np.mean(sampled, axis=-1))
    return np.stack(output, axis=0).astype(np.float32)


def evaluate_cross_band_candidate(
    candidate_azimuth_deg: float,
    candidate_elevation_deg: float | None,
    responses: np.ndarray,
    bundle: SubbandGccBundle,
    azimuths_deg: np.ndarray,
    elevations_deg: np.ndarray | None = None,
    thresholds: CrossBandThresholds = CrossBandThresholds(),
) -> CrossBandDecision:
    """Evaluate whether a candidate is repeated across energized subbands."""

    maps = np.asarray(responses, dtype=np.float32)
    azimuths = np.asarray(azimuths_deg, dtype=np.float32).reshape(-1)
    elevations = (
        None
        if elevations_deg is None
        else np.asarray(elevations_deg, dtype=np.float32).reshape(-1)
    )
    expected_shape = (
        (len(bundle.bands), azimuths.size)
        if elevations is None
        else (len(bundle.bands), elevations.size, azimuths.size)
    )
    if maps.shape != expected_shape:
        raise ValueError(f"responses must have shape {expected_shape}, got {maps.shape}")
    if elevations is None and candidate_elevation_deg is not None:
        raise ValueError("1-D responses require candidate_elevation_deg=None")
    if elevations is not None and candidate_elevation_deg is None:
        raise ValueError("2-D responses require candidate elevation")

    if elevations is None:
        coordinates = [(float(azimuth), None) for azimuth in azimuths]
    else:
        coordinates = [
            (float(azimuth), float(elevation))
            for elevation in elevations
            for azimuth in azimuths
        ]
    distances = np.asarray(
        [
            angular_distance_deg(
                candidate_azimuth_deg,
                candidate_elevation_deg,
                azimuth,
                elevation,
            )
            for azimuth, elevation in coordinates
        ],
        dtype=np.float32,
    )
    neighborhood = distances <= float(thresholds.support_radius_deg)
    if not np.any(neighborhood):
        raise ValueError("candidate has no grid point inside support radius")

    supports: list[BandCandidateSupport] = []
    supported_offsets: list[float] = []
    for band_index, band in enumerate(bundle.bands):
        flat = maps[band_index].reshape(-1)
        active = float(bundle.relative_energy[band_index]) >= float(
            thresholds.minimum_relative_band_energy
        )
        low = float(np.percentile(flat, 5.0))
        maximum = float(np.max(flat))
        dynamic = maximum - low
        neighborhood_indices = np.flatnonzero(neighborhood)
        local_index = int(
            neighborhood_indices[
                int(np.argmax(flat[neighborhood_indices]))
            ]
        )
        normalized = (
            0.0
            if dynamic <= 1e-12
            else float(np.clip((float(flat[local_index]) - low) / dynamic, 0.0, 1.0))
        )
        azimuth, elevation = coordinates[local_index]
        offset = float(distances[local_index])
        supported = bool(
            active
            and dynamic > 1e-12
            and normalized >= float(thresholds.minimum_local_response)
        )
        if supported:
            supported_offsets.append(offset)
        supports.append(
            BandCandidateSupport(
                band_name=band.name,
                relative_energy=float(bundle.relative_energy[band_index]),
                active=active,
                dynamic_range=dynamic,
                local_peak_azimuth_deg=azimuth,
                local_peak_elevation_deg=elevation,
                local_peak_offset_deg=offset,
                normalized_local_response=normalized,
                supported=supported,
            )
        )
    active_count = int(sum(item.active for item in supports))
    supported_count = int(sum(item.supported for item in supports))
    median_offset = (
        None if not supported_offsets else float(np.median(supported_offsets))
    )
    consistency = supported_count / max(len(bundle.bands), 1)
    if active_count < int(thresholds.minimum_active_bands):
        reason = "REJECT_TOO_FEW_ENERGIZED_BANDS"
    elif supported_count < int(thresholds.minimum_supported_bands):
        reason = "REJECT_TOO_FEW_DIRECTIONAL_BANDS"
    elif median_offset is None or median_offset > float(
        thresholds.maximum_median_offset_deg
    ):
        reason = "REJECT_CROSS_BAND_DIRECTION_SPREAD"
    else:
        reason = "ACCEPT_CROSS_BAND_CONSISTENCY"
    return CrossBandDecision(
        accepted=reason == "ACCEPT_CROSS_BAND_CONSISTENCY",
        reason=reason,
        active_band_count=active_count,
        supported_band_count=supported_count,
        consistency_ratio=float(consistency),
        median_supported_offset_deg=median_offset,
        band_support=tuple(supports),
    )

