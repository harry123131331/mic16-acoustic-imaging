"""Direction-independent temporal dynamics for candidate event rejection.

The spatial frontend already decides *where* a coherent event is.  This small
software-only block tests whether the steered candidate beam changes over time
like active speech rather than remaining stationary like the current fan
fixture.  It does not identify a speaker, alter a direction, or create a new
candidate.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from .candidate_speech_activity import extract_speech_activity_features


CANDIDATE_EVENT_DYNAMICS_VERSION = "mic16_candidate_event_dynamics_dev1"


@dataclass(frozen=True)
class CandidateEventDynamicsFeatures:
    frame_count: int
    level_std_db: float
    level_p90_p10_db: float
    spectral_entropy_std: float
    periodicity_std: float

    def to_dict(self) -> dict[str, float | int]:
        return asdict(self)


@dataclass(frozen=True)
class CandidateEventDynamicsThresholds:
    minimum_level_std_db: float = 2.30
    minimum_spectral_entropy_std: float = 0.080

    def __post_init__(self) -> None:
        if not 0.0 <= float(self.minimum_level_std_db) <= 40.0:
            raise ValueError("minimum_level_std_db must be in [0,40]")
        if not 0.0 <= float(self.minimum_spectral_entropy_std) <= 1.0:
            raise ValueError("minimum_spectral_entropy_std must be in [0,1]")

    def to_dict(self) -> dict[str, float]:
        return asdict(self)


@dataclass(frozen=True)
class CandidateEventDynamicsDecision:
    accepted: bool
    reason: str
    features: CandidateEventDynamicsFeatures

    def to_dict(self) -> dict[str, object]:
        return {
            "accepted": bool(self.accepted),
            "reason": self.reason,
            "features": self.features.to_dict(),
        }


def extract_candidate_event_dynamics(
    beam_frames: np.ndarray,
    sample_rate: float,
) -> CandidateEventDynamicsFeatures:
    """Summarize frame-to-frame dynamics of one fixed candidate direction."""

    values = np.asarray(beam_frames, dtype=np.float32)
    if values.ndim != 2 or values.shape[0] < 4 or values.shape[1] < 64:
        raise ValueError("beam_frames must have shape [frame>=4,sample>=64]")
    if not np.isfinite(values).all():
        raise ValueError("beam_frames must be finite")
    per_frame = [
        extract_speech_activity_features(frame, float(sample_rate)) for frame in values
    ]
    levels = np.asarray([item.log_rms_dbfs for item in per_frame], dtype=np.float64)
    entropies = np.asarray([item.spectral_entropy for item in per_frame], dtype=np.float64)
    periodicities = np.asarray([item.periodicity for item in per_frame], dtype=np.float64)
    return CandidateEventDynamicsFeatures(
        frame_count=int(values.shape[0]),
        level_std_db=float(np.std(levels)),
        level_p90_p10_db=float(np.percentile(levels, 90.0) - np.percentile(levels, 10.0)),
        spectral_entropy_std=float(np.std(entropies)),
        periodicity_std=float(np.std(periodicities)),
    )


def decide_candidate_event_dynamics(
    features: CandidateEventDynamicsFeatures,
    thresholds: CandidateEventDynamicsThresholds = CandidateEventDynamicsThresholds(),
) -> CandidateEventDynamicsDecision:
    """Accept only candidates with both level and spectral-shape dynamics."""

    if int(features.frame_count) < 4:
        raise ValueError("features require at least four frames")
    if float(features.level_std_db) < float(thresholds.minimum_level_std_db):
        reason = "REJECT_STATIONARY_LEVEL"
    elif float(features.spectral_entropy_std) < float(
        thresholds.minimum_spectral_entropy_std
    ):
        reason = "REJECT_STATIONARY_SPECTRAL_SHAPE"
    else:
        reason = "ACCEPT_SPEECH_LIKE_DYNAMICS"
    return CandidateEventDynamicsDecision(
        accepted=reason == "ACCEPT_SPEECH_LIKE_DYNAMICS",
        reason=reason,
        features=features,
    )

