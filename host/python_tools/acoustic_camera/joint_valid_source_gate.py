"""Ordered B3--B6 valid-active-source gate for a shared MIC16 candidate.

The module only combines already implemented, interpretable evidence.  It does
not create candidates, tune thresholds, run temporal tracking, or modify the
formal one-dimensional PL/PS contract.  B7 remains the existing causal
``TemporalSourceConfirmer`` applied to B6-accepted candidates.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, replace

from .candidate_directness import (
    DirectnessDecision,
    DirectnessFeatures,
    DirectnessThresholds,
    apply_directness_gate,
)
from .candidate_speech_activity import (
    SpeechActivityDecision,
    SpeechActivityFeatures,
    SpeechActivityThresholds,
    decide_speech_activity,
)
from .cross_band_consistency import CrossBandDecision
from .valid_source_detection import (
    CandidatePeak,
    ValidityThresholds,
    apply_spatial_validity_gate,
)


JOINT_VALID_SOURCE_GATE_VERSION = "mic16_joint_valid_active_source_b3_b7_v1"


@dataclass(frozen=True)
class JointGateThresholds:
    spatial: ValidityThresholds
    directness: DirectnessThresholds
    speech_activity: SpeechActivityThresholds

    def to_dict(self) -> dict[str, object]:
        return {
            "spatial": asdict(self.spatial),
            "directness": self.directness.to_dict(),
            "speech_activity": self.speech_activity.to_dict(),
        }


@dataclass(frozen=True)
class JointCandidateDecision:
    candidate: CandidatePeak
    cross_band: CrossBandDecision
    directness: DirectnessDecision
    speech_activity: SpeechActivityDecision
    b3_accepted: bool
    b4_accepted: bool
    b5_accepted: bool
    b6_accepted: bool
    first_rejection_stage: str
    final_reason: str

    def to_dict(self) -> dict[str, object]:
        return {
            "b3_accepted": self.b3_accepted,
            "b4_accepted": self.b4_accepted,
            "b5_accepted": self.b5_accepted,
            "b6_accepted": self.b6_accepted,
            "first_rejection_stage": self.first_rejection_stage,
            "final_reason": self.final_reason,
        }

    def temporal_candidate(self) -> CandidatePeak:
        """Return the same candidate marked for the causal B7 confirmer."""

        return replace(
            self.candidate,
            accepted=bool(self.b6_accepted),
            rejection_reason=self.final_reason,
        )


def evaluate_joint_candidate(
    candidate: CandidatePeak,
    cross_band: CrossBandDecision,
    directness_features: DirectnessFeatures,
    speech_features: SpeechActivityFeatures,
    noise_floor_dbfs: float,
    thresholds: JointGateThresholds,
) -> JointCandidateDecision:
    """Apply B3, B4, B5 and B6 in the frozen cascade order."""

    spatial = apply_spatial_validity_gate((candidate,), thresholds.spatial)[0]
    directness = apply_directness_gate(directness_features, thresholds.directness)
    speech = decide_speech_activity(
        speech_features,
        float(noise_floor_dbfs),
        thresholds.speech_activity,
    )
    b3 = bool(spatial.accepted)
    b4 = bool(b3 and cross_band.accepted)
    b5 = bool(b4 and directness.accepted)
    b6 = bool(b5 and speech.active)
    if not b3:
        first = "B3"
        reason = spatial.rejection_reason
    elif not b4:
        first = "B4"
        reason = cross_band.reason
    elif not b5:
        first = "B5"
        reason = directness.reason
    elif not b6:
        first = "B6"
        reason = speech.reason
    else:
        first = "NONE"
        reason = "ACCEPT_B6_JOINT_GATE"
    return JointCandidateDecision(
        candidate=spatial,
        cross_band=cross_band,
        directness=directness,
        speech_activity=speech,
        b3_accepted=b3,
        b4_accepted=b4,
        b5_accepted=b5,
        b6_accepted=b6,
        first_rejection_stage=first,
        final_reason=reason,
    )
