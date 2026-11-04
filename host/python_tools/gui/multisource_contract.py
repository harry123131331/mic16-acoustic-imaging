"""Versioned GUI contract for anonymous zero-to-many 2-D sound sources.

The contract deliberately separates raw spatial candidates from effective
sources accepted by the frozen event-dynamics gate.  It carries no speaker
identity and does not call its evidence value a calibrated probability.
"""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass
from typing import Any


MULTISOURCE_SCHEMA_VERSION = "mic16.multisource.localization.v1"


@dataclass(frozen=True)
class SpatialCandidate2D:
    rank: int
    azimuth_deg: float
    elevation_deg: float
    accepted: bool
    decision_reason: str
    vote_count: int
    frame_support: int
    coarse_band_support: int
    zone_support: int
    mean_spatial_coherence: float
    mean_energy_excess_db: float
    level_std_db: float
    spectral_entropy_std: float
    level_gate_margin_db: float
    entropy_gate_margin: float

    def to_payload(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class EffectiveSource2D:
    source_id: str
    candidate_rank: int
    azimuth_deg: float
    elevation_deg: float
    anonymous: bool
    evidence_kind: str
    level_gate_margin_db: float
    entropy_gate_margin: float

    def to_payload(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class MultisourceLocalizationResult:
    window_id: str
    status: str
    algorithm: str
    calibration_id: str
    frame_count: int
    history_seconds: float
    sources: tuple[EffectiveSource2D, ...]
    spatial_candidates: tuple[SpatialCandidate2D, ...]
    schema_version: str = MULTISOURCE_SCHEMA_VERSION
    coordinate_convention: str = "azimuth_right_positive_elevation_up_positive_deg"
    source_semantics: str = "anonymous_effective_acoustic_sources_not_speaker_identities"
    evidence_semantics: str = "frozen_rule_margins_not_calibrated_probability"

    def to_payload(self) -> dict[str, Any]:
        payload = asdict(self)
        payload["source_count"] = len(self.sources)
        payload["candidate_count"] = len(self.spatial_candidates)
        return payload


def validate_multisource_payload(payload: dict[str, Any]) -> None:
    """Reject malformed or semantically ambiguous GUI payloads."""

    if payload.get("schema_version") != MULTISOURCE_SCHEMA_VERSION:
        raise ValueError("unsupported multisource schema_version")
    if payload.get("status") not in {"OK", "NO_VALID_SOURCE"}:
        raise ValueError("status must be OK or NO_VALID_SOURCE")
    if not str(payload.get("window_id", "")).strip():
        raise ValueError("window_id is required")
    if not str(payload.get("algorithm", "")).strip():
        raise ValueError("algorithm is required")
    if not str(payload.get("calibration_id", "")).strip():
        raise ValueError("calibration_id is required")
    frame_count = int(payload.get("frame_count", 0))
    history_seconds = float(payload.get("history_seconds", 0.0))
    if frame_count < 4 or not math.isfinite(history_seconds) or history_seconds <= 0.0:
        raise ValueError("invalid frame history")
    sources = list(payload.get("sources", []))
    candidates = list(payload.get("spatial_candidates", []))
    if int(payload.get("source_count", -1)) != len(sources):
        raise ValueError("source_count mismatch")
    if int(payload.get("candidate_count", -1)) != len(candidates):
        raise ValueError("candidate_count mismatch")
    if (payload["status"] == "OK") != bool(sources):
        raise ValueError("status/source_count mismatch")

    ranks: set[int] = set()
    candidate_lookup: dict[int, dict[str, Any]] = {}
    for candidate in candidates:
        rank = int(candidate["rank"])
        if rank < 1 or rank in ranks:
            raise ValueError("candidate ranks must be unique positive integers")
        ranks.add(rank)
        candidate_lookup[rank] = candidate
        _validate_direction(candidate)
        if int(candidate["vote_count"]) < 1 or int(candidate["frame_support"]) < 1:
            raise ValueError("candidate support must be positive")
        if not str(candidate.get("decision_reason", "")).strip():
            raise ValueError("candidate decision_reason is required")

    source_ids: set[str] = set()
    for source in sources:
        _validate_direction(source)
        source_id = str(source.get("source_id", ""))
        if not source_id or source_id in source_ids:
            raise ValueError("source_id must be nonempty and unique")
        source_ids.add(source_id)
        if source.get("anonymous") is not True:
            raise ValueError("effective sources must remain anonymous")
        rank = int(source["candidate_rank"])
        candidate = candidate_lookup.get(rank)
        if candidate is None or candidate.get("accepted") is not True:
            raise ValueError("source must reference an accepted candidate")
        if abs(float(source["azimuth_deg"]) - float(candidate["azimuth_deg"])) > 1e-9:
            raise ValueError("source/candidate azimuth mismatch")
        if abs(float(source["elevation_deg"]) - float(candidate["elevation_deg"])) > 1e-9:
            raise ValueError("source/candidate elevation mismatch")
        if source.get("evidence_kind") != "frozen_event_dynamics_gate_margin":
            raise ValueError("unsupported evidence_kind")


def _validate_direction(item: dict[str, Any]) -> None:
    azimuth = float(item["azimuth_deg"])
    elevation = float(item["elevation_deg"])
    if not math.isfinite(azimuth) or not -180.0 <= azimuth <= 180.0:
        raise ValueError("azimuth_deg out of range")
    if not math.isfinite(elevation) or not -90.0 <= elevation <= 90.0:
        raise ValueError("elevation_deg out of range")

