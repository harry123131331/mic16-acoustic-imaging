"""Identity-free short-time / cross-band 2-D direction *candidate* generation.

This PC-only research component does not estimate source count or declare a
candidate to be a valid sound source.  It preserves evidence that is lost when
five speech frames are averaged into one GCC/SRP response.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, replace

import numpy as np

from mic16_config import Mic16Config

from .geometry_2d import AzElGrid, build_azel_lag_table
from .speech_sequence_gcc import extract_sequence_gcc_phat_features
from .srp_phat_2d import srp_phat_azel_response
from .valid_source_detection import angular_distance_deg, extract_candidate_peaks


MULTIVIEW_CANDIDATE_VERSION = "mic16_multiview_azel_candidates_v1"


@dataclass(frozen=True)
class MultiViewConfig:
    bands_hz: tuple[tuple[float, float], ...] = ((300.0, 1500.0), (1500.0, 3500.0), (3500.0, 8000.0))
    peaks_per_view: int = 4
    view_nms_radius_deg: float = 15.0
    fusion_radius_deg: float = 10.0
    output_nms_radius_deg: float = 15.0
    max_candidates: int = 8

    def __post_init__(self) -> None:
        if not self.bands_hz or any(low < 0 or high <= low for low, high in self.bands_hz):
            raise ValueError("bands_hz must contain increasing, nonempty bands")
        if self.peaks_per_view < 1 or self.max_candidates < 1:
            raise ValueError("candidate budgets must be positive")
        if min(self.view_nms_radius_deg, self.fusion_radius_deg, self.output_nms_radius_deg) <= 0:
            raise ValueError("angular radii must be positive")


@dataclass(frozen=True)
class ViewPeak:
    frame_index: int
    band_index: int
    azimuth_deg: float
    elevation_deg: float
    prominence: float
    normalized_peak: float


@dataclass(frozen=True)
class MultiViewCandidate:
    azimuth_deg: float
    elevation_deg: float
    view_support: int
    frame_support: int
    band_support: int
    prominence_sum: float
    rank: int

    def to_dict(self) -> dict[str, float | int]:
        return asdict(self)


def fuse_view_peaks(
    peaks: tuple[ViewPeak, ...] | list[ViewPeak],
    config: MultiViewConfig = MultiViewConfig(),
) -> tuple[MultiViewCandidate, ...]:
    """Rank observed peak directions by distinct supporting frame-band views.

    Every seed is itself an observed SRP local maximum.  A view can cast at
    most one vote near a seed, so broad or duplicated peaks from one response
    cannot inflate the support count.  No acceptance threshold is applied.
    """

    if not peaks:
        return ()
    if any(peak.frame_index < 0 or peak.band_index < 0 for peak in peaks):
        raise ValueError("view indices must be nonnegative")
    seeds: list[tuple[float, float, int, int, int, float]] = []
    for seed in peaks:
        nearest_by_view: dict[tuple[int, int], ViewPeak] = {}
        for peak in peaks:
            if angular_distance_deg(
                seed.azimuth_deg, seed.elevation_deg, peak.azimuth_deg, peak.elevation_deg
            ) > config.fusion_radius_deg:
                continue
            key = peak.frame_index, peak.band_index
            previous = nearest_by_view.get(key)
            if previous is None or (peak.prominence, peak.normalized_peak) > (
                previous.prominence, previous.normalized_peak
            ):
                nearest_by_view[key] = peak
        support = tuple(nearest_by_view.values())
        seeds.append((
            seed.azimuth_deg,
            seed.elevation_deg,
            len(support),
            len({peak.frame_index for peak in support}),
            len({peak.band_index for peak in support}),
            float(sum(max(peak.prominence, 0.0) for peak in support)),
        ))
    seeds.sort(key=lambda item: (item[2], item[3], item[4], item[5]), reverse=True)
    selected: list[MultiViewCandidate] = []
    for azimuth, elevation, views, frames, bands, prominence in seeds:
        if any(
            angular_distance_deg(azimuth, elevation, item.azimuth_deg, item.elevation_deg)
            < config.output_nms_radius_deg
            for item in selected
        ):
            continue
        selected.append(MultiViewCandidate(
            azimuth_deg=float(azimuth), elevation_deg=float(elevation),
            view_support=int(views), frame_support=int(frames), band_support=int(bands),
            prominence_sum=prominence, rank=len(selected) + 1,
        ))
        if len(selected) >= config.max_candidates:
            break
    return tuple(selected)


def extract_multiview_candidates(
    frames: np.ndarray,
    cfg: Mic16Config = Mic16Config(),
    grid: AzElGrid = AzElGrid(),
    config: MultiViewConfig = MultiViewConfig(),
) -> tuple[MultiViewCandidate, ...]:
    """Get anonymous 2-D candidates from every short frame and frequency band."""

    values = np.asarray(frames)
    if values.ndim != 3 or values.shape[1:] != (cfg.frame_samples, cfg.channels) or values.shape[0] < 1:
        raise ValueError("frames must have shape [positive_frame_count, frame_samples, channels]")
    if not np.isfinite(values).all():
        raise ValueError("frames contain non-finite values")
    if max(high for _, high in config.bands_hz) > cfg.sample_rate / 2:
        raise ValueError("frequency band exceeds Nyquist")
    lag_table = build_azel_lag_table(grid, cfg)
    peaks: list[ViewPeak] = []
    for frame_index, frame in enumerate(values):
        for band_index, (low_hz, high_hz) in enumerate(config.bands_hz):
            band_cfg = replace(cfg, bandpass_low_hz=low_hz, bandpass_high_hz=high_hz)
            gcc = extract_sequence_gcc_phat_features(
                frame[None, ...], band_cfg, mode="frame_mean", window="hann", bandpass=True
            )
            response, _ = srp_phat_azel_response(gcc, lag_table, cfg)
            extraction = extract_candidate_peaks(
                response, grid.azimuths_deg, grid.elevations_deg,
                nms_radius_deg=config.view_nms_radius_deg,
                max_candidates=config.peaks_per_view,
            )
            peaks.extend(ViewPeak(
                frame_index=frame_index, band_index=band_index,
                azimuth_deg=float(candidate.azimuth_deg),
                elevation_deg=float(candidate.elevation_deg),
                prominence=float(candidate.prominence),
                normalized_peak=float(candidate.normalized_peak),
            ) for candidate in extraction.nms_candidates)
    return fuse_view_peaks(peaks, config)
