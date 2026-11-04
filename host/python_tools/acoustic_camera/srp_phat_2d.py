"""Fractional-lag SRP-PHAT azimuth/elevation baseline for MIC16."""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from mic16_config import Mic16Config

from .geometry_2d import AzElGrid, great_circle_error_deg


SRP_AZEL_FORMULA_VERSION = "mic16_srp_phat_azel_fractional_lag_v1"


@dataclass(frozen=True)
class AzElPeak:
    azimuth_deg: float
    elevation_deg: float
    response: float
    relative_response_db: float

    def to_dict(self) -> dict[str, float]:
        return asdict(self)


@dataclass(frozen=True)
class AzElEstimate:
    azimuth_deg: float | None
    elevation_deg: float | None
    confidence: float
    valid: bool
    reason: str
    peak_response: float
    active_pair_count: int
    top_peaks: tuple[AzElPeak, ...]

    def to_dict(self) -> dict[str, object]:
        payload = asdict(self)
        payload["top_peaks"] = [peak.to_dict() for peak in self.top_peaks]
        return payload


def _as_pair_lag(
    gcc_features: np.ndarray,
    pair_count: int,
    cfg: Mic16Config,
) -> np.ndarray:
    values = np.asarray(gcc_features, dtype=np.float32)
    lag_count = 2 * int(cfg.max_lag) + 1
    if values.ndim == 1 and values.size == pair_count * lag_count:
        return values.reshape(pair_count, lag_count)
    if values.shape == (pair_count, lag_count):
        return values
    raise ValueError(
        f"gcc_features must be [{pair_count},{lag_count}] or flat length "
        f"{pair_count * lag_count}, got {values.shape}"
    )


def interpolate_pair_curves(
    pair_lag_curves: np.ndarray,
    fractional_lags: np.ndarray,
    cfg: Mic16Config = Mic16Config(),
) -> np.ndarray:
    """Sample all pair curves at fractional lags.

    Curves have shape ``[pair,lag]``.  The last dimension of
    ``fractional_lags`` must be ``pair``; the returned shape matches the lag
    table and therefore supports both a flattened grid and ``[E,A,pair]``.
    """

    curves = np.asarray(pair_lag_curves, dtype=np.float32)
    lags = np.asarray(fractional_lags, dtype=np.float32)
    if curves.ndim != 2:
        raise ValueError("pair_lag_curves must have shape [pair,lag]")
    if lags.ndim < 2 or lags.shape[-1] != curves.shape[0]:
        raise ValueError("fractional_lags last dimension must equal pair count")
    expected_lag_count = 2 * int(cfg.max_lag) + 1
    if curves.shape[1] != expected_lag_count:
        raise ValueError(
            f"pair curves require {expected_lag_count} lags, got {curves.shape[1]}"
        )

    positions = np.clip(
        lags + float(cfg.max_lag), 0.0, float(2 * int(cfg.max_lag))
    )
    lower = np.floor(positions).astype(np.int64)
    upper = np.minimum(lower + 1, 2 * int(cfg.max_lag)).astype(np.int64)
    fraction = positions - lower.astype(np.float32)
    pair_indices = np.arange(curves.shape[0], dtype=np.int64)
    lower_values = curves[pair_indices, lower]
    upper_values = curves[pair_indices, upper]
    return (
        lower_values * (1.0 - fraction) + upper_values * fraction
    ).astype(np.float32)


def srp_phat_azel_response(
    gcc_features: np.ndarray,
    lag_table: np.ndarray,
    cfg: Mic16Config = Mic16Config(),
    *,
    pair_weights: np.ndarray | None = None,
    pair_enable_mask: np.ndarray | None = None,
) -> tuple[np.ndarray, int]:
    """Return a response map with shape ``[elevation,azimuth]``.

    Weights are static/calibration weights by default.  Dynamic reliability is
    deliberately not enabled implicitly because it has not passed the frozen
    two-dimensional validation gate.
    """

    table = np.asarray(lag_table, dtype=np.float32)
    if table.ndim != 3:
        raise ValueError("lag_table must have shape [elevation,azimuth,pair]")
    pair_count = int(table.shape[2])
    curves = _as_pair_lag(gcc_features, pair_count, cfg)
    if pair_weights is None:
        weights = np.ones(pair_count, dtype=np.float32)
    else:
        weights = np.asarray(pair_weights, dtype=np.float32).reshape(-1)
        if weights.size != pair_count:
            raise ValueError(f"pair_weights must contain {pair_count} values")
        weights = np.clip(weights, 0.0, None)
    if pair_enable_mask is not None:
        mask = np.asarray(pair_enable_mask, dtype=np.float32).reshape(-1)
        if mask.size != pair_count:
            raise ValueError(f"pair_enable_mask must contain {pair_count} values")
        weights = weights * (mask > 0.0).astype(np.float32)
    active_pair_count = int(np.count_nonzero(weights > 0.0))
    if active_pair_count == 0:
        return np.zeros(table.shape[:2], dtype=np.float32), 0

    sampled = interpolate_pair_curves(curves, table, cfg)
    response = np.sum(sampled * weights.reshape(1, 1, -1), axis=2)
    response /= float(np.sum(weights) + 1e-8)
    return response.astype(np.float32), active_pair_count


def relative_response_db(response: np.ndarray, floor_db: float = -30.0) -> np.ndarray:
    """Convert algorithm response to relative dB; this is not absolute SPL."""

    values = np.asarray(response, dtype=np.float64)
    if values.ndim != 2 or values.size == 0:
        raise ValueError("response must be a non-empty two-dimensional map")
    shifted = values - float(np.min(values))
    scale = float(np.max(shifted))
    if scale <= 1e-12:
        return np.full(values.shape, float(floor_db), dtype=np.float32)
    normalized = np.clip(shifted / scale, 10.0 ** (float(floor_db) / 10.0), 1.0)
    return np.maximum(10.0 * np.log10(normalized), float(floor_db)).astype(np.float32)


def _top_peaks(
    response: np.ndarray,
    grid: AzElGrid,
    count: int,
    suppression_radius_deg: float,
) -> tuple[AzElPeak, ...]:
    values = np.asarray(response, dtype=np.float32)
    db = relative_response_db(values)
    order = np.argsort(values.reshape(-1))[::-1]
    selected: list[AzElPeak] = []
    for flat_index in order:
        elevation_index, azimuth_index = np.unravel_index(int(flat_index), values.shape)
        azimuth = float(grid.azimuths_deg[azimuth_index])
        elevation = float(grid.elevations_deg[elevation_index])
        if any(
            great_circle_error_deg(
                azimuth,
                elevation,
                peak.azimuth_deg,
                peak.elevation_deg,
            )
            < float(suppression_radius_deg)
            for peak in selected
        ):
            continue
        selected.append(
            AzElPeak(
                azimuth_deg=azimuth,
                elevation_deg=elevation,
                response=float(values[elevation_index, azimuth_index]),
                relative_response_db=float(db[elevation_index, azimuth_index]),
            )
        )
        if len(selected) >= int(count):
            break
    return tuple(selected)


def estimate_azimuth_elevation(
    response: np.ndarray,
    grid: AzElGrid = AzElGrid(),
    *,
    active_pair_count: int = 32,
    minimum_active_pairs: int = 8,
    minimum_confidence: float = 0.05,
    top_k: int = 3,
) -> AzElEstimate:
    """Estimate the front-hemisphere peak and a bounded sharpness score.

    ``confidence`` is a relative peak-sharpness diagnostic, not a calibrated
    correctness probability.  A learned calibration must be fitted on a
    separate validation set before deployment.
    """

    values = np.asarray(response, dtype=np.float32)
    if values.shape != grid.shape:
        raise ValueError(f"response shape must be {grid.shape}, got {values.shape}")
    if not np.all(np.isfinite(values)):
        return AzElEstimate(None, None, 0.0, False, "non_finite_response", 0.0, int(active_pair_count), ())
    if int(active_pair_count) < int(minimum_active_pairs):
        return AzElEstimate(None, None, 0.0, False, "too_few_active_pairs", 0.0, int(active_pair_count), ())
    dynamic_range = float(np.max(values) - np.min(values))
    if dynamic_range <= 1e-10:
        return AzElEstimate(None, None, 0.0, False, "flat_response", float(np.max(values)), int(active_pair_count), ())

    peaks = _top_peaks(
        values,
        grid,
        count=max(1, int(top_k)),
        suppression_radius_deg=max(
            float(grid.azimuth_step_deg), float(grid.elevation_step_deg)
        ) * 1.5,
    )
    primary = peaks[0]
    median = float(np.median(values))
    robust_scale = float(np.percentile(values, 95.0) - np.percentile(values, 5.0))
    prominence = max(0.0, float(primary.response) - median)
    sharpness = prominence / (abs(prominence) + max(robust_scale, 1e-8))
    confidence = float(np.clip(sharpness, 0.0, 1.0))
    valid = confidence >= float(minimum_confidence)
    return AzElEstimate(
        azimuth_deg=primary.azimuth_deg if valid else None,
        elevation_deg=primary.elevation_deg if valid else None,
        confidence=confidence,
        valid=valid,
        reason="ok" if valid else "low_relative_peak_confidence",
        peak_response=float(primary.response),
        active_pair_count=int(active_pair_count),
        top_peaks=peaks,
    )
