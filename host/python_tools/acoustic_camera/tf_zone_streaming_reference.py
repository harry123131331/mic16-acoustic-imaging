"""Memory-bounded streaming reference for the frozen TF-zone grid scan.

The original PC implementation precomputes ``[grid,pair,bin]`` complex
steering signatures.  This module stores only one initial phasor and one
per-bin rotator for each ``[grid,pair]`` and advances the phase while bins
arrive in order.  It is a deployment reference for D3, not a new algorithm.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from mic16_config import Mic16Config, compact32_pairs

from .geometry_2d import AzElGrid, build_azel_lag_table


TF_ZONE_STREAMING_REFERENCE_VERSION = "mic16_tf_zone_streaming_recurrence_d3_v1"


@dataclass(frozen=True)
class TfZoneStreamingState:
    first_bin: int
    bin_count: int
    pair_count: int
    grid_point_count: int
    initial_phasor: np.ndarray
    bin_rotator: np.ndarray

    @property
    def persistent_bytes(self) -> int:
        return int(self.initial_phasor.nbytes + self.bin_rotator.nbytes)


def build_tf_zone_streaming_state(
    cfg: Mic16Config,
    grid: AzElGrid,
    frequency_bins: np.ndarray,
) -> TfZoneStreamingState:
    bins = np.asarray(frequency_bins, dtype=np.int64).reshape(-1)
    if bins.size < 1 or np.any(np.diff(bins) != 1):
        raise ValueError("streaming recurrence requires consecutive frequency bins")
    if int(bins[0]) < 0 or int(bins[-1]) > int(cfg.fft_size) // 2:
        raise ValueError("frequency bins exceed the real-FFT range")
    lags = build_azel_lag_table(grid, cfg).reshape(grid.point_count, -1).astype(np.float64)
    pair_count = int(compact32_pairs(cfg).shape[0])
    if lags.shape != (grid.point_count, pair_count):
        raise RuntimeError("lag table shape does not match the frozen grid/pair contract")
    radians_per_bin = 2.0 * np.pi * lags / float(cfg.fft_size)
    initial = np.exp(1j * radians_per_bin * int(bins[0])).astype(np.complex64)
    rotator = np.exp(1j * radians_per_bin).astype(np.complex64)
    return TfZoneStreamingState(
        first_bin=int(bins[0]),
        bin_count=int(bins.size),
        pair_count=pair_count,
        grid_point_count=int(grid.point_count),
        initial_phasor=initial,
        bin_rotator=rotator,
    )


def stream_tf_zone_scores(
    observation: np.ndarray,
    zone_slices: tuple[slice, ...],
    state: TfZoneStreamingState,
) -> np.ndarray:
    """Return normalized scores as ``[zone,grid]`` without a signature table."""

    observed = np.asarray(observation, dtype=np.complex64)
    expected = (int(state.pair_count), int(state.bin_count))
    if observed.shape != expected:
        raise ValueError(f"observation must have shape {expected}, got {observed.shape}")
    if not np.isfinite(observed).all():
        raise ValueError("observation contains non-finite values")
    expected_start = 0
    for zone in zone_slices:
        if zone.step not in (None, 1) or zone.start != expected_start or zone.stop <= zone.start:
            raise ValueError("zone slices must form a contiguous forward partition")
        expected_start = int(zone.stop)
    if expected_start != int(state.bin_count):
        raise ValueError("zone slices do not cover all streaming bins")

    phasor = state.initial_phasor.copy()
    scores = np.empty((len(zone_slices), state.grid_point_count), dtype=np.float32)
    for zone_index, zone in enumerate(zone_slices):
        accumulator = np.zeros(state.grid_point_count, dtype=np.float32)
        for bin_index in range(int(zone.start), int(zone.stop)):
            product = np.conj(phasor) * observed[:, bin_index][None, :]
            accumulator += np.sum(np.real(product), axis=1, dtype=np.float32)
            phasor *= state.bin_rotator
        scores[zone_index] = accumulator / np.float32(
            state.pair_count * (int(zone.stop) - int(zone.start))
        )
    return scores
