"""Deterministic software-simulation service for the GUI Az/El lab page."""

from __future__ import annotations

import time
from dataclasses import asdict, dataclass

import numpy as np

from mic16_config import Mic16Config, compact32_pairs
from mic16_gcc_phat_features import extract_gcc_phat_features

from .geometry_2d import AzElGrid, build_azel_lag_table, great_circle_error_deg
from .simulate_2d import ReflectionPath, simulate_farfield_frame
from .srp_phat_2d import (
    estimate_azimuth_elevation,
    relative_response_db,
    srp_phat_azel_response,
)


AZEL_GUI_SIMULATION_CONTRACT = "mic16_azel_gui_software_simulation_v1"
AZEL_SIMULATION_CONDITIONS = (
    "标称链路",
    "通道失配",
    "失配 + 1 条反射",
    "失配 + 2 条反射",
)


@dataclass(frozen=True)
class AzElSimulationRequest:
    truth_azimuth_deg: float = 20.0
    truth_elevation_deg: float = 10.0
    snr_db: float = -5.0
    condition: str = "失配 + 2 条反射"
    frames: int = 5
    seed: int = 20260913

    def validate(self, grid: AzElGrid = AzElGrid()) -> None:
        if not grid.azimuth_min_deg <= float(self.truth_azimuth_deg) <= grid.azimuth_max_deg:
            raise ValueError("truth azimuth is outside the front-hemisphere GUI grid")
        if not grid.elevation_min_deg <= float(self.truth_elevation_deg) <= grid.elevation_max_deg:
            raise ValueError("truth elevation is outside the front-hemisphere GUI grid")
        if self.condition not in AZEL_SIMULATION_CONDITIONS:
            raise ValueError(f"unsupported simulation condition: {self.condition}")
        if not 1 <= int(self.frames) <= 20:
            raise ValueError("frames must be in [1,20]")


def condition_parameters(condition: str) -> tuple[float, float, int]:
    mapping = {
        "标称链路": (0.35, 0.05, 0),
        "通道失配": (0.60, 0.20, 0),
        "失配 + 1 条反射": (0.60, 0.20, 1),
        "失配 + 2 条反射": (0.60, 0.20, 2),
    }
    try:
        return mapping[condition]
    except KeyError as exc:
        raise ValueError(f"unsupported simulation condition: {condition}") from exc


def _sample_reflections(
    request: AzElSimulationRequest,
    grid: AzElGrid,
    count: int,
) -> tuple[ReflectionPath, ...]:
    rng = np.random.default_rng(int(request.seed) + 7001)
    paths: list[ReflectionPath] = []
    while len(paths) < int(count):
        azimuth = float(rng.uniform(grid.azimuth_min_deg, grid.azimuth_max_deg))
        elevation = float(rng.uniform(grid.elevation_min_deg, grid.elevation_max_deg))
        if great_circle_error_deg(
            request.truth_azimuth_deg,
            request.truth_elevation_deg,
            azimuth,
            elevation,
        ) < 15.0:
            continue
        paths.append(
            ReflectionPath(
                azimuth_deg=azimuth,
                elevation_deg=elevation,
                relative_gain=float(rng.uniform(0.12, 0.30)),
                extra_delay_samples=float(rng.uniform(12.0, 55.0)),
            )
        )
    return tuple(paths)


def run_azel_gui_simulation(
    request: AzElSimulationRequest,
    *,
    cfg: Mic16Config = Mic16Config(),
    grid: AzElGrid = AzElGrid(),
) -> dict[str, object]:
    """Run one complete synthetic PCM-to-GCC-to-2D-SRP GUI scenario."""

    request.validate(grid)
    gain_std_db, delay_std_samples, reflection_count = condition_parameters(
        request.condition
    )
    reflections = _sample_reflections(request, grid, reflection_count)
    lag_table = build_azel_lag_table(grid, cfg)
    responses: list[np.ndarray] = []
    active_pair_counts: list[int] = []
    frame_rms: list[float] = []
    started = time.perf_counter()
    for frame_index in range(int(request.frames)):
        frame, _ = simulate_farfield_frame(
            request.truth_azimuth_deg,
            request.truth_elevation_deg,
            cfg,
            snr_db=request.snr_db,
            seed=int(request.seed) + frame_index * 37,
            nonideal_seed=int(request.seed) + 13,
            channel_gain_std_db=gain_std_db,
            channel_delay_std_samples=delay_std_samples,
            reflections=reflections,
        )
        gcc = extract_gcc_phat_features(frame, cfg=cfg)
        response, active_pairs = srp_phat_azel_response(gcc, lag_table, cfg)
        responses.append(np.asarray(response, dtype=np.float32))
        active_pair_counts.append(int(active_pairs))
        centered = frame.astype(np.float64) - np.mean(frame, axis=0, keepdims=True)
        frame_rms.append(float(np.sqrt(np.mean(np.square(centered)))))

    mean_response = np.mean(np.stack(responses, axis=0), axis=0).astype(np.float32)
    active_pairs = min(active_pair_counts)
    estimate = estimate_azimuth_elevation(
        mean_response,
        grid,
        active_pair_count=active_pairs,
        minimum_confidence=0.0,
    )
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    if estimate.valid:
        spherical_error = great_circle_error_deg(
            request.truth_azimuth_deg,
            request.truth_elevation_deg,
            float(estimate.azimuth_deg),
            float(estimate.elevation_deg),
        )
    else:
        spherical_error = None
    return {
        "contract_version": AZEL_GUI_SIMULATION_CONTRACT,
        "evidence_level": "controlled_software_simulation_not_measurement",
        "request": asdict(request),
        "configuration": {
            "sample_rate_hz": float(cfg.sample_rate),
            "frame_samples": int(cfg.frame_samples),
            "channel_count": int(cfg.channels),
            "pair_count": int(compact32_pairs(cfg).shape[0]),
            "channel_gain_std_db": float(gain_std_db),
            "channel_delay_std_samples": float(delay_std_samples),
            "reflection_count": int(reflection_count),
            "reflections": [asdict(path) for path in reflections],
            "grid_shape_elevation_azimuth": list(grid.shape),
        },
        "estimate": estimate.to_dict(),
        "spherical_error_deg": spherical_error,
        "active_pair_count": int(active_pairs),
        "mean_frame_rms": float(np.mean(frame_rms)),
        "processing_latency_ms": float(elapsed_ms),
        "azimuths_deg": grid.azimuths_deg.astype(np.float32),
        "elevations_deg": grid.elevations_deg.astype(np.float32),
        "response": mean_response,
        "response_db": relative_response_db(mean_response),
        "claim_boundary": (
            "GUI software simulation through synthetic 16-channel PCM, production PC "
            "GCC-PHAT and experimental 2-D SRP. Not measured-room, HLS, Vivado, ILA, "
            "or FPGA evidence."
        ),
    }
