"""Versioned azimuth/elevation geometry for the isolated MIC16 2-D prototype.

The existing ``mic16_circle16_geometry_v1`` contract describes a one-
dimensional angle around the ring and remains untouched.  This module instead
defines a front-facing array coordinate system for acoustic-camera research:

* x: image right
* y: image up
* z: array normal / camera forward
* azimuth: from +z toward +x
* elevation: from the x-z plane toward +y

All microphones lie in z=0, so directions mirrored across the array plane have
identical ideal delays.  The default grid therefore covers the front
hemisphere only; that assumption is part of the returned metadata.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Iterable

import numpy as np

from mic16_config import Mic16Config, compact32_pairs, mic_positions_xy


AZEL_GEOMETRY_CONTRACT_VERSION = "mic16_planar_front_azel_geometry_v1"


@dataclass(frozen=True)
class AzElGrid:
    """Regular front-hemisphere search grid, in degrees."""

    azimuth_min_deg: float = -60.0
    azimuth_max_deg: float = 60.0
    azimuth_step_deg: float = 5.0
    elevation_min_deg: float = -30.0
    elevation_max_deg: float = 30.0
    elevation_step_deg: float = 5.0

    def __post_init__(self) -> None:
        if self.azimuth_step_deg <= 0.0 or self.elevation_step_deg <= 0.0:
            raise ValueError("azimuth/elevation steps must be positive")
        if self.azimuth_min_deg > self.azimuth_max_deg:
            raise ValueError("azimuth_min_deg must not exceed azimuth_max_deg")
        if self.elevation_min_deg > self.elevation_max_deg:
            raise ValueError("elevation_min_deg must not exceed elevation_max_deg")
        if self.azimuth_min_deg <= -90.0 or self.azimuth_max_deg >= 90.0:
            raise ValueError("front-hemisphere azimuth limits must lie inside (-90, 90)")
        if self.elevation_min_deg <= -90.0 or self.elevation_max_deg >= 90.0:
            raise ValueError("elevation limits must lie inside (-90, 90)")

    @staticmethod
    def _inclusive_grid(lower: float, upper: float, step: float) -> np.ndarray:
        count = int(round((float(upper) - float(lower)) / float(step))) + 1
        values = float(lower) + np.arange(count, dtype=np.float64) * float(step)
        if values.size == 0 or abs(float(values[-1]) - float(upper)) > 1e-6:
            raise ValueError("grid range must be an integer multiple of its step")
        return values.astype(np.float32)

    @property
    def azimuths_deg(self) -> np.ndarray:
        return self._inclusive_grid(
            self.azimuth_min_deg, self.azimuth_max_deg, self.azimuth_step_deg
        )

    @property
    def elevations_deg(self) -> np.ndarray:
        return self._inclusive_grid(
            self.elevation_min_deg,
            self.elevation_max_deg,
            self.elevation_step_deg,
        )

    @property
    def shape(self) -> tuple[int, int]:
        return (int(self.elevations_deg.size), int(self.azimuths_deg.size))

    @property
    def point_count(self) -> int:
        elevation_count, azimuth_count = self.shape
        return elevation_count * azimuth_count

    def to_dict(self) -> dict[str, object]:
        payload = asdict(self)
        payload.update(
            {
                "shape_elevation_azimuth": list(self.shape),
                "point_count": self.point_count,
                "front_hemisphere_only": True,
            }
        )
        return payload


def direction_unit_vector(
    azimuth_deg: float | np.ndarray,
    elevation_deg: float | np.ndarray,
    *,
    normal_sign: float = 1.0,
) -> np.ndarray:
    """Return ``[...,3]`` unit vectors in the front-facing array frame.

    ``normal_sign=-1`` is provided only for the planar mirror-ambiguity test.
    Normal operation must use the default positive/front normal.
    """

    if float(normal_sign) not in (-1.0, 1.0):
        raise ValueError("normal_sign must be +1 or -1")
    azimuth, elevation = np.broadcast_arrays(
        np.asarray(azimuth_deg, dtype=np.float64),
        np.asarray(elevation_deg, dtype=np.float64),
    )
    a = np.deg2rad(azimuth)
    e = np.deg2rad(elevation)
    vector = np.stack(
        [
            np.cos(e) * np.sin(a),
            np.sin(e),
            float(normal_sign) * np.cos(e) * np.cos(a),
        ],
        axis=-1,
    )
    return vector.astype(np.float32)


def mic_positions_xyz(cfg: Mic16Config = Mic16Config()) -> np.ndarray:
    """Return the existing circular PCB positions embedded in the z=0 plane."""

    xy = mic_positions_xy(cfg).astype(np.float32)
    return np.column_stack([xy, np.zeros(xy.shape[0], dtype=np.float32)])


def far_field_arrival_delays_samples_azel(
    azimuth_deg: float,
    elevation_deg: float,
    cfg: Mic16Config = Mic16Config(),
    *,
    normal_sign: float = 1.0,
) -> np.ndarray:
    """Return per-microphone relative delays for one far-field direction."""

    direction = direction_unit_vector(
        azimuth_deg, elevation_deg, normal_sign=normal_sign
    ).reshape(3)
    positions = mic_positions_xyz(cfg)
    delays = -(positions @ direction) / float(cfg.sound_speed) * float(cfg.sample_rate)
    delays -= float(np.mean(delays))
    return delays.astype(np.float32)


def expected_pair_lags_azel(
    azimuth_deg: float,
    elevation_deg: float,
    cfg: Mic16Config = Mic16Config(),
    pairs: np.ndarray | None = None,
    *,
    channel_delay_offsets_samples: Iterable[float] | np.ndarray | None = None,
    normal_sign: float = 1.0,
    clip: bool = True,
) -> np.ndarray:
    """Return pair lags where positive means channel b arrives after a."""

    pair_array = compact32_pairs(cfg) if pairs is None else np.asarray(pairs, dtype=np.int64)
    if pair_array.ndim != 2 or pair_array.shape[1] != 2:
        raise ValueError("pairs must have shape [pair_count,2]")
    delays = far_field_arrival_delays_samples_azel(
        azimuth_deg,
        elevation_deg,
        cfg,
        normal_sign=normal_sign,
    )
    if channel_delay_offsets_samples is not None:
        offsets = np.asarray(channel_delay_offsets_samples, dtype=np.float32).reshape(-1)
        if offsets.size != int(cfg.channels):
            raise ValueError(
                f"channel_delay_offsets_samples must have {cfg.channels} values"
            )
        delays = delays + offsets
    lags = delays[pair_array[:, 1]] - delays[pair_array[:, 0]]
    if clip:
        lags = np.clip(lags, -float(cfg.max_lag), float(cfg.max_lag))
    return lags.astype(np.float32)


def build_azel_lag_table(
    grid: AzElGrid = AzElGrid(),
    cfg: Mic16Config = Mic16Config(),
    pairs: np.ndarray | None = None,
    *,
    channel_delay_offsets_samples: Iterable[float] | np.ndarray | None = None,
) -> np.ndarray:
    """Build fractional pair-lag table with shape ``[elevation,azimuth,pair]``."""

    pair_array = compact32_pairs(cfg) if pairs is None else np.asarray(pairs, dtype=np.int64)
    elevations = grid.elevations_deg
    azimuths = grid.azimuths_deg
    table = np.empty(
        (elevations.size, azimuths.size, pair_array.shape[0]), dtype=np.float32
    )
    for elevation_index, elevation in enumerate(elevations):
        for azimuth_index, azimuth in enumerate(azimuths):
            table[elevation_index, azimuth_index] = expected_pair_lags_azel(
                float(azimuth),
                float(elevation),
                cfg,
                pair_array,
                channel_delay_offsets_samples=channel_delay_offsets_samples,
                clip=True,
            )
    return table


def grid_direction_vectors(grid: AzElGrid = AzElGrid()) -> np.ndarray:
    """Return grid vectors with shape ``[elevation,azimuth,3]``."""

    azimuth_mesh, elevation_mesh = np.meshgrid(
        grid.azimuths_deg, grid.elevations_deg
    )
    return direction_unit_vector(azimuth_mesh, elevation_mesh)


def great_circle_error_deg(
    true_azimuth_deg: float,
    true_elevation_deg: float,
    predicted_azimuth_deg: float,
    predicted_elevation_deg: float,
) -> float:
    """Return the spherical angle between two front-hemisphere directions."""

    truth = direction_unit_vector(true_azimuth_deg, true_elevation_deg).reshape(3).astype(np.float64)
    predicted = direction_unit_vector(
        predicted_azimuth_deg, predicted_elevation_deg
    ).reshape(3).astype(np.float64)
    truth /= max(float(np.linalg.norm(truth)), 1e-15)
    predicted /= max(float(np.linalg.norm(predicted)), 1e-15)
    cosine = float(np.clip(np.dot(truth, predicted), -1.0, 1.0))
    if 1.0 - cosine <= 1e-12:
        return 0.0
    return float(np.rad2deg(np.arccos(cosine)))


def geometry_metadata(
    grid: AzElGrid = AzElGrid(), cfg: Mic16Config = Mic16Config()
) -> dict[str, object]:
    """Return provenance metadata for result figures and datasets."""

    return {
        "contract_version": AZEL_GEOMETRY_CONTRACT_VERSION,
        "coordinate_axes": {"x": "image_right", "y": "image_up", "z": "camera_forward"},
        "direction_formula": "[cos(e)*sin(a), sin(e), cos(e)*cos(a)]",
        "array_plane": "z=0",
        "front_hemisphere_only": True,
        "planar_mirror_ambiguity": "u_z and -u_z have identical ideal delays",
        "sample_rate_hz": float(cfg.sample_rate),
        "sound_speed_m_s": float(cfg.sound_speed),
        "array_radius_m": float(cfg.array_radius_m),
        "channel_count": int(cfg.channels),
        "pair_count": int(compact32_pairs(cfg).shape[0]),
        "grid": grid.to_dict(),
    }
