"""Orthogonal PHAT scans for anonymous multi-source Az/El candidates.

This PC-side research module follows the structural idea of multi-source
SVD-PHAT: after each scan, project the phase-normalized cross-spectrum onto a
subspace orthogonal to the steering signature already found, then scan again.
Here the projection is performed in the original PHAT space (no low-rank SVD
compression or nearest-neighbour acceleration), which keeps the experiment
small and auditable for the fixed MIC16 grid.

The output is still a candidate list.  It does not infer source count or
decide whether a coherent direction is speech, machinery, or a reflection.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from mic16_channel_map import apply_channel_map, default_channel_map
from mic16_config import Mic16Config, compact32_pairs

from .geometry_2d import AzElGrid, build_azel_lag_table
from .multiview_candidates_2d import (
    MultiViewCandidate,
    MultiViewConfig,
    ViewPeak,
    fuse_view_peaks,
)
from .valid_source_detection import angular_distance_deg


ORTHOGONAL_PHAT_CANDIDATE_VERSION = "mic16_orthogonal_phat_azel_candidates_dev1"


@dataclass(frozen=True)
class OrthogonalPhatConfig:
    bands_hz: tuple[tuple[float, float], ...] = (
        (300.0, 1500.0),
        (1500.0, 3500.0),
        (3500.0, 8000.0),
    )
    scans_per_view: int = 4
    scan_nms_radius_deg: float = 15.0
    minimum_normalized_score: float = 0.0

    def __post_init__(self) -> None:
        if not self.bands_hz or any(low < 0.0 or high <= low for low, high in self.bands_hz):
            raise ValueError("bands_hz must contain increasing nonempty bands")
        if int(self.scans_per_view) < 1:
            raise ValueError("scans_per_view must be positive")
        if float(self.scan_nms_radius_deg) <= 0.0:
            raise ValueError("scan_nms_radius_deg must be positive")
        if not 0.0 <= float(self.minimum_normalized_score) <= 1.0:
            raise ValueError("minimum_normalized_score must be in [0,1]")


@dataclass(frozen=True)
class OrthogonalPhatPeak:
    grid_index: int
    azimuth_deg: float
    elevation_deg: float
    scan_index: int
    score: float
    normalized_score: float
    residual_fraction: float


def band_frequency_bins(
    cfg: Mic16Config, low_hz: float, high_hz: float
) -> np.ndarray:
    if low_hz < 0.0 or high_hz <= low_hz or high_hz > float(cfg.sample_rate) / 2.0:
        raise ValueError("invalid PHAT frequency band")
    frequency = np.fft.rfftfreq(int(cfg.fft_size), d=1.0 / float(cfg.sample_rate))
    bins = np.flatnonzero((frequency >= float(low_hz)) & (frequency <= float(high_hz)))
    if bins.size == 0:
        raise ValueError("frequency band contains no FFT bins")
    return bins.astype(np.int64)


def build_phat_signatures(
    grid: AzElGrid,
    cfg: Mic16Config,
    frequency_bins: np.ndarray,
) -> np.ndarray:
    """Return expected unit-magnitude cross spectra as ``[grid,features]``."""

    bins = np.asarray(frequency_bins, dtype=np.int64).reshape(-1)
    if bins.size == 0 or np.any(bins < 0) or np.any(bins > int(cfg.fft_size) // 2):
        raise ValueError("invalid frequency bins")
    lag_table = build_azel_lag_table(grid, cfg)
    lags = lag_table.reshape(-1, lag_table.shape[-1]).astype(np.float64)
    phase = (
        2.0
        * np.pi
        * lags[:, :, None]
        * bins.reshape(1, 1, -1)
        / float(cfg.fft_size)
    )
    return np.exp(1j * phase).reshape(grid.point_count, -1).astype(np.complex64)


def extract_frame_phat_observation(
    frame: np.ndarray,
    cfg: Mic16Config,
    frequency_bins: np.ndarray,
) -> np.ndarray:
    """Return pair-major phase-normalized cross-spectrum observations."""

    values = np.asarray(frame, dtype=np.float64)
    if values.shape != (int(cfg.frame_samples), int(cfg.channels)):
        raise ValueError("frame shape does not match MIC16 configuration")
    if not np.isfinite(values).all():
        raise ValueError("frame must be finite")
    logical = apply_channel_map(values, default_channel_map(cfg), cfg)
    logical -= np.mean(logical, axis=0, keepdims=True)
    logical *= np.hanning(logical.shape[0])[:, None]
    spectra = np.fft.rfft(logical, n=int(cfg.fft_size), axis=0)
    bins = np.asarray(frequency_bins, dtype=np.int64).reshape(-1)
    pairs = compact32_pairs(cfg)
    cross = np.stack(
        [
            spectra[bins, int(channel_a)]
            * np.conj(spectra[bins, int(channel_b)])
            for channel_a, channel_b in pairs
        ],
        axis=0,
    )
    magnitude = np.abs(cross)
    phat = np.divide(
        cross,
        magnitude,
        out=np.zeros_like(cross),
        where=magnitude > float(cfg.phat_epsilon),
    )
    return phat.reshape(-1).astype(np.complex64)


def orthogonal_phat_scans(
    observation: np.ndarray,
    signatures: np.ndarray,
    grid: AzElGrid,
    config: OrthogonalPhatConfig = OrthogonalPhatConfig(),
) -> tuple[OrthogonalPhatPeak, ...]:
    """Repeated PHAT scans with Gram-Schmidt source-signature removal."""

    values = np.asarray(observation, dtype=np.complex128).reshape(-1)
    atoms = np.asarray(signatures, dtype=np.complex128)
    if atoms.ndim != 2 or atoms.shape != (grid.point_count, values.size):
        raise ValueError("signature shape does not match observation and grid")
    if not np.isfinite(values).all() or not np.isfinite(atoms).all():
        raise ValueError("observation and signatures must be finite")
    initial_norm = float(np.linalg.norm(values))
    if initial_norm <= 1e-12:
        return ()
    atom_norms = np.linalg.norm(atoms, axis=1)
    residual = values.copy()
    selected: list[int] = []
    basis: list[np.ndarray] = []
    peaks: list[OrthogonalPhatPeak] = []
    azimuths, elevations = grid.azimuths_deg, grid.elevations_deg
    for scan_index in range(int(config.scans_per_view)):
        scores = np.real(np.conj(atoms) @ residual)
        normalized = scores / np.maximum(atom_norms * initial_norm, 1e-12)
        for index in range(grid.point_count):
            elevation_index, azimuth_index = np.unravel_index(index, grid.shape)
            azimuth = float(azimuths[azimuth_index])
            elevation = float(elevations[elevation_index])
            if any(
                angular_distance_deg(
                    azimuth,
                    elevation,
                    float(azimuths[np.unravel_index(other, grid.shape)[1]]),
                    float(elevations[np.unravel_index(other, grid.shape)[0]]),
                )
                < float(config.scan_nms_radius_deg)
                for other in selected
            ):
                normalized[index] = -np.inf
        index = int(np.argmax(normalized))
        best = float(normalized[index])
        if not np.isfinite(best) or best <= float(config.minimum_normalized_score):
            break
        vector = atoms[index].copy()
        for unit in basis:
            vector -= unit * np.vdot(unit, vector)
        vector_norm = float(np.linalg.norm(vector))
        if vector_norm <= 1e-10:
            break
        unit = vector / vector_norm
        selected.append(index)
        basis.append(unit)
        residual -= unit * np.vdot(unit, residual)
        elevation_index, azimuth_index = np.unravel_index(index, grid.shape)
        peaks.append(
            OrthogonalPhatPeak(
                grid_index=index,
                azimuth_deg=float(azimuths[azimuth_index]),
                elevation_deg=float(elevations[elevation_index]),
                scan_index=scan_index + 1,
                score=float(scores[index]),
                normalized_score=best,
                residual_fraction=float(np.linalg.norm(residual) / initial_norm),
            )
        )
    return tuple(peaks)


def extract_orthogonal_phat_candidates(
    frames: np.ndarray,
    cfg: Mic16Config = Mic16Config(),
    grid: AzElGrid = AzElGrid(),
    scan_config: OrthogonalPhatConfig = OrthogonalPhatConfig(),
    fusion_config: MultiViewConfig = MultiViewConfig(),
) -> tuple[MultiViewCandidate, ...]:
    """Fuse repeated orthogonal-PHAT scans across frames and bands."""

    values = np.asarray(frames)
    if values.ndim != 3 or values.shape[1:] != (cfg.frame_samples, cfg.channels) or values.shape[0] < 1:
        raise ValueError("frames must have shape [positive_frame_count, frame_samples, channels]")
    if not np.isfinite(values).all():
        raise ValueError("frames contain non-finite values")
    view_peaks: list[ViewPeak] = []
    for band_index, (low_hz, high_hz) in enumerate(scan_config.bands_hz):
        bins = band_frequency_bins(cfg, low_hz, high_hz)
        signatures = build_phat_signatures(grid, cfg, bins)
        for frame_index, frame in enumerate(values):
            observation = extract_frame_phat_observation(frame, cfg, bins)
            for peak in orthogonal_phat_scans(observation, signatures, grid, scan_config):
                view_peaks.append(
                    ViewPeak(
                        frame_index=frame_index,
                        band_index=band_index,
                        azimuth_deg=peak.azimuth_deg,
                        elevation_deg=peak.elevation_deg,
                        prominence=max(peak.normalized_score, 0.0),
                        normalized_peak=max(peak.normalized_score, 0.0),
                    )
                )
    return fuse_view_peaks(view_peaks, fusion_config)

