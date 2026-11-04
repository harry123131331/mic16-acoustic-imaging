"""Sparse residual Az/El candidate probe for the MIC16 PC research path.

The ordinary SRP peak list repeatedly reads the same dirty-map sidelobes as
new directions.  This experimental frontend instead represents one
frame/band GCC observation with a small non-negative set of ideal far-field
GCC atoms.  After every selected atom, all selected amplitudes are refitted
and the next direction is read from the residual.  It is deliberately kept
separate from the frozen multi-view baseline.

This is candidate generation only.  It neither decides the number of valid
sources nor classifies speech/noise, and it is not a CLEAN-SC implementation.
"""

from __future__ import annotations

from dataclasses import dataclass, replace

import numpy as np
from scipy.optimize import nnls

from mic16_config import Mic16Config

from .geometry_2d import AzElGrid, build_azel_lag_table
from .multiview_candidates_2d import (
    MultiViewCandidate,
    MultiViewConfig,
    ViewPeak,
    fuse_view_peaks,
)
from .speech_sequence_gcc import extract_sequence_gcc_phat_features
from .valid_source_detection import angular_distance_deg


SPARSE_RESIDUAL_CANDIDATE_VERSION = "mic16_sparse_residual_azel_candidates_dev1"


@dataclass(frozen=True)
class SparseResidualConfig:
    bands_hz: tuple[tuple[float, float], ...] = (
        (300.0, 1500.0),
        (1500.0, 3500.0),
        (3500.0, 8000.0),
    )
    atoms_per_view: int = 4
    atom_nms_radius_deg: float = 15.0
    minimum_relative_correlation: float = 0.02

    def __post_init__(self) -> None:
        if not self.bands_hz or any(low < 0.0 or high <= low for low, high in self.bands_hz):
            raise ValueError("bands_hz must contain increasing nonempty bands")
        if int(self.atoms_per_view) < 1:
            raise ValueError("atoms_per_view must be positive")
        if float(self.atom_nms_radius_deg) <= 0.0:
            raise ValueError("atom_nms_radius_deg must be positive")
        if not 0.0 <= float(self.minimum_relative_correlation) <= 1.0:
            raise ValueError("minimum_relative_correlation must be in [0,1]")


@dataclass(frozen=True)
class SparseAtomPeak:
    grid_index: int
    azimuth_deg: float
    elevation_deg: float
    correlation: float
    relative_correlation: float
    fitted_amplitude: float


def build_bandlimited_gcc_dictionary(
    grid: AzElGrid,
    cfg: Mic16Config,
    low_hz: float,
    high_hz: float,
) -> np.ndarray:
    """Return unit-norm ideal PHAT-GCC atoms as ``[pair*lag, grid]``.

    Each pair curve is the band-limited correlation expected for the
    fractional far-field lag.  The dictionary uses the same lag convention
    as the SRP sampler.
    """

    if low_hz < 0.0 or high_hz <= low_hz or high_hz > float(cfg.sample_rate) / 2.0:
        raise ValueError("invalid dictionary frequency band")
    lag_table = build_azel_lag_table(grid, cfg)
    grid_lags = lag_table.reshape(-1, lag_table.shape[-1]).astype(np.float64)
    frequency = np.fft.rfftfreq(int(cfg.fft_size), d=1.0 / float(cfg.sample_rate))
    active_bins = np.flatnonzero((frequency >= float(low_hz)) & (frequency <= float(high_hz)))
    if active_bins.size == 0:
        raise ValueError("frequency band contains no FFT bins")
    lag_axis = np.arange(-int(cfg.max_lag), int(cfg.max_lag) + 1, dtype=np.float64)
    delta = lag_axis.reshape(1, 1, -1) - grid_lags[:, :, None]
    angle = 2.0 * np.pi * delta / float(cfg.fft_size)
    first_bin = int(active_bins[0])
    last_bin = int(active_bins[-1])
    bin_count = int(active_bins.size)
    # cos(k×角度)在指定频点范围的闭式均值，利用sinc
    # 比值避免构造网格×通道对×延迟×频率四维张量。
    envelope = np.sinc(bin_count * angle / (2.0 * np.pi)) / np.sinc(
        angle / (2.0 * np.pi)
    )
    atoms = envelope * np.cos(0.5 * (first_bin + last_bin) * angle)
    dictionary = atoms.transpose(1, 2, 0).reshape(-1, grid_lags.shape[0])
    norms = np.linalg.norm(dictionary, axis=0)
    dictionary /= np.maximum(norms, 1e-12)
    return dictionary.astype(np.float32)


def select_sparse_residual_atoms(
    gcc_features: np.ndarray,
    dictionary: np.ndarray,
    grid: AzElGrid,
    config: SparseResidualConfig = SparseResidualConfig(),
) -> tuple[SparseAtomPeak, ...]:
    """Greedy non-negative residual fit with spherical NMS."""

    observed = np.asarray(gcc_features, dtype=np.float64).reshape(-1)
    atoms = np.asarray(dictionary, dtype=np.float64)
    if atoms.ndim != 2 or atoms.shape[0] != observed.size or atoms.shape[1] != grid.point_count:
        raise ValueError("dictionary shape does not match GCC vector and grid")
    if not np.isfinite(observed).all() or not np.isfinite(atoms).all():
        raise ValueError("GCC and dictionary must be finite")
    norm = float(np.linalg.norm(observed))
    if norm <= 1e-12:
        return ()
    residual = observed.copy()
    selected: list[int] = []
    raw_correlations: list[float] = []
    amplitudes = np.zeros(0, dtype=np.float64)
    azimuths = grid.azimuths_deg
    elevations = grid.elevations_deg
    for _ in range(int(config.atoms_per_view)):
        correlations = atoms.T @ residual
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
                < float(config.atom_nms_radius_deg)
                for other in selected
            ):
                correlations[index] = -np.inf
        index = int(np.argmax(correlations))
        correlation = float(correlations[index])
        relative = correlation / max(norm, 1e-12)
        if not np.isfinite(correlation) or correlation <= 0.0:
            break
        if selected and relative < float(config.minimum_relative_correlation):
            break
        selected.append(index)
        raw_correlations.append(correlation)
        amplitudes, _ = nnls(atoms[:, selected], observed)
        residual = observed - atoms[:, selected] @ amplitudes
    peaks: list[SparseAtomPeak] = []
    for position, index in enumerate(selected):
        elevation_index, azimuth_index = np.unravel_index(index, grid.shape)
        peaks.append(
            SparseAtomPeak(
                grid_index=index,
                azimuth_deg=float(azimuths[azimuth_index]),
                elevation_deg=float(elevations[elevation_index]),
                correlation=float(raw_correlations[position]),
                relative_correlation=float(raw_correlations[position] / max(norm, 1e-12)),
                fitted_amplitude=float(amplitudes[position]),
            )
        )
    return tuple(peaks)


def extract_sparse_residual_candidates(
    frames: np.ndarray,
    cfg: Mic16Config = Mic16Config(),
    grid: AzElGrid = AzElGrid(),
    sparse_config: SparseResidualConfig = SparseResidualConfig(),
    fusion_config: MultiViewConfig = MultiViewConfig(),
) -> tuple[MultiViewCandidate, ...]:
    """Fuse sparse residual directions across short frames and bands."""

    values = np.asarray(frames)
    if values.ndim != 3 or values.shape[1:] != (cfg.frame_samples, cfg.channels) or values.shape[0] < 1:
        raise ValueError("frames must have shape [positive_frame_count, frame_samples, channels]")
    if not np.isfinite(values).all():
        raise ValueError("frames contain non-finite values")
    view_peaks: list[ViewPeak] = []
    for band_index, (low_hz, high_hz) in enumerate(sparse_config.bands_hz):
        dictionary = build_bandlimited_gcc_dictionary(grid, cfg, low_hz, high_hz)
        band_cfg = replace(cfg, bandpass_low_hz=low_hz, bandpass_high_hz=high_hz)
        for frame_index, frame in enumerate(values):
            gcc = extract_sequence_gcc_phat_features(
                frame[None, ...], band_cfg, mode="frame_mean", window="hann", bandpass=True
            )
            for atom in select_sparse_residual_atoms(gcc, dictionary, grid, sparse_config):
                view_peaks.append(
                    ViewPeak(
                        frame_index=frame_index,
                        band_index=band_index,
                        azimuth_deg=atom.azimuth_deg,
                        elevation_deg=atom.elevation_deg,
                        prominence=max(atom.fitted_amplitude, 0.0),
                        normalized_peak=max(atom.relative_correlation, 0.0),
                    )
                )
    return fuse_view_peaks(view_peaks, fusion_config)
