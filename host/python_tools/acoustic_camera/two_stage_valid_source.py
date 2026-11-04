"""Two-stage speech-activity and direction selection for MIC16 research.

Stage 1 aggregates candidate-beam evidence into one direction-invariant frame
decision: speech present or absent.  Stage 2 is evaluated only while Stage 1
is active and ranks the spatial candidates.  The separation prevents
direction-label errors from being treated as frame-level non-speech examples.

This module belongs to the PC research branch.  It does not change the formal
FPGA ABI or claim real-array performance.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from .probabilistic_speech_presence import SPEECH_PRESENCE_FEATURE_NAMES


TWO_STAGE_VALID_SOURCE_VERSION = "mic16_two_stage_valid_source_v1"

FRAME_ACTIVITY_FEATURE_NAMES = (
    "candidate_count_log1p",
    "b6c_probability_max",
    "b6c_probability_mean",
    "b6c_probability_std",
    "b6c_probability_top_margin",
) + tuple(
    f"b6s_max_{name}" for name in SPEECH_PRESENCE_FEATURE_NAMES
) + tuple(
    f"b6s_median_{name}" for name in SPEECH_PRESENCE_FEATURE_NAMES
)


def aggregate_frame_activity_features(
    b6c_probabilities: np.ndarray,
    b6s_features: np.ndarray,
) -> np.ndarray:
    """Aggregate per-candidate evidence without selecting a direction."""

    probabilities = np.asarray(b6c_probabilities, dtype=np.float64).reshape(-1)
    speech = np.asarray(b6s_features, dtype=np.float64)
    feature_count = len(SPEECH_PRESENCE_FEATURE_NAMES)
    if speech.ndim != 2 or speech.shape != (probabilities.size, feature_count):
        raise ValueError(
            f"b6s_features must have shape [candidate,{feature_count}]"
        )
    if not np.all(np.isfinite(probabilities)) or not np.all(np.isfinite(speech)):
        raise ValueError("frame activity inputs must be finite")
    if probabilities.size == 0:
        return np.zeros(len(FRAME_ACTIVITY_FEATURE_NAMES), dtype=np.float64)
    ordered = np.sort(probabilities)[::-1]
    margin = float(ordered[0] - ordered[1]) if ordered.size > 1 else float(ordered[0])
    values = np.concatenate(
        (
            np.asarray(
                [
                    np.log1p(float(probabilities.size)),
                    float(np.max(probabilities)),
                    float(np.mean(probabilities)),
                    float(np.std(probabilities)),
                    margin,
                ],
                dtype=np.float64,
            ),
            np.max(speech, axis=0),
            np.median(speech, axis=0),
        )
    )
    if values.size != len(FRAME_ACTIVITY_FEATURE_NAMES) or not np.all(
        np.isfinite(values)
    ):
        raise RuntimeError("frame activity feature schema mismatch")
    return values


@dataclass(frozen=True)
class FrameActivityGateConfig:
    activation_threshold: float
    release_threshold: float
    birth_hits: int = 1
    release_hits: int = 1

    def __post_init__(self) -> None:
        for name in ("activation_threshold", "release_threshold"):
            value = float(getattr(self, name))
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} must be in [0,1]")
        if int(self.birth_hits) < 1 or int(self.release_hits) < 1:
            raise ValueError("birth_hits and release_hits must be positive")

    def to_dict(self) -> dict[str, float | int]:
        return asdict(self)


class CausalFrameActivityGate:
    """Small causal state machine with independently registered release."""

    def __init__(self, config: FrameActivityGateConfig) -> None:
        self.config = config
        self.reset()

    def reset(self) -> None:
        self.active = False
        self.pending_birth_hits = 0
        self.pending_release_hits = 0

    def update(self, probability: float) -> bool:
        value = float(probability)
        if not np.isfinite(value) or not 0.0 <= value <= 1.0:
            raise ValueError("activity probability must be finite and in [0,1]")
        if self.active:
            if value < float(self.config.release_threshold):
                self.pending_release_hits += 1
                if self.pending_release_hits >= int(self.config.release_hits):
                    self.active = False
                    self.pending_release_hits = 0
                    self.pending_birth_hits = 0
            else:
                self.pending_release_hits = 0
            return self.active
        if value >= float(self.config.activation_threshold):
            self.pending_birth_hits += 1
            if self.pending_birth_hits >= int(self.config.birth_hits):
                self.active = True
                self.pending_birth_hits = 0
                self.pending_release_hits = 0
        else:
            self.pending_birth_hits = 0
        return self.active


def select_direction_candidate(
    *,
    frame_active: bool,
    direction_probabilities: np.ndarray,
    minimum_probability: float,
    minimum_margin: float,
) -> int | None:
    """Return the deterministic best candidate index or ``None``."""

    probabilities = np.asarray(direction_probabilities, dtype=np.float64).reshape(-1)
    if not np.all(np.isfinite(probabilities)):
        raise ValueError("direction probabilities must be finite")
    if not 0.0 <= float(minimum_probability) <= 1.0:
        raise ValueError("minimum_probability must be in [0,1]")
    if float(minimum_margin) < 0.0:
        raise ValueError("minimum_margin must be non-negative")
    if not frame_active or probabilities.size == 0:
        return None
    order = np.argsort(-probabilities, kind="stable")
    winner = int(order[0])
    runner_up = float(probabilities[int(order[1])]) if order.size > 1 else 0.0
    if float(probabilities[winner]) < float(minimum_probability):
        return None
    if float(probabilities[winner]) - runner_up < float(minimum_margin):
        return None
    return winner
