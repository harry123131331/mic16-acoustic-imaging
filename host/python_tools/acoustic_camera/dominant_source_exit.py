"""Causal activity/continuity exit gate for a single dominant candidate.

This module is deliberately placed after the frozen B6 speech gate and the
B6D per-frame dominant-candidate arbitration.  It does not create spatial
candidates or modify B6.  Its purpose is to reject residual stop-tail and
noise winners before they enter the B7 source confirmer.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

from .valid_source_detection import angular_distance_deg


DOMINANT_SOURCE_EXIT_VERSION = "mic16_causal_dominant_source_exit_v1"


@dataclass(frozen=True)
class CausalExitConfig:
    minimum_activity_score: float = 0.20
    minimum_energy_excess_db: float = 4.0
    immediate_activity_score: float = 0.40
    birth_hits: int = 2
    association_radius_deg: float = 20.0

    def __post_init__(self) -> None:
        for name in ("minimum_activity_score", "immediate_activity_score"):
            value = float(getattr(self, name))
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} must be in [0,1]")
        if float(self.immediate_activity_score) < float(self.minimum_activity_score):
            raise ValueError("immediate_activity_score must be >= minimum_activity_score")
        if float(self.minimum_energy_excess_db) < 0.0:
            raise ValueError("minimum_energy_excess_db must be non-negative")
        if int(self.birth_hits) < 1:
            raise ValueError("birth_hits must be positive")
        if float(self.association_radius_deg) <= 0.0:
            raise ValueError("association_radius_deg must be positive")

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class CausalExitDecision:
    accepted: bool
    reason: str
    activity_score: float
    energy_excess_db: float
    pending_hits: int
    active_before: bool
    active_after: bool


class CausalDominantSourceExitGate:
    """Stateful causal gate with immediate high-confidence admission.

    A strong candidate may enter immediately.  A weaker-but-valid candidate
    must repeat near the same direction for ``birth_hits`` frames.  Once a
    source is active, low activity or a direction jump causes an immediate
    exit; there is intentionally no hangover because B7 owns persistence.
    """

    def __init__(self, config: CausalExitConfig = CausalExitConfig()) -> None:
        self.config = config
        self.active = False
        self.azimuth_deg: float | None = None
        self.elevation_deg: float | None = None
        self.pending_azimuth_deg: float | None = None
        self.pending_elevation_deg: float | None = None
        self.pending_hits = 0

    def _clear_pending(self) -> None:
        self.pending_azimuth_deg = None
        self.pending_elevation_deg = None
        self.pending_hits = 0

    def _activate(self, azimuth_deg: float, elevation_deg: float) -> None:
        self.active = True
        self.azimuth_deg = float(azimuth_deg)
        self.elevation_deg = float(elevation_deg)
        self._clear_pending()

    def _deactivate(self) -> None:
        self.active = False
        self.azimuth_deg = None
        self.elevation_deg = None

    def update(
        self,
        candidate: tuple[float, float, float, float] | None,
    ) -> CausalExitDecision:
        """Update with ``(azimuth, elevation, activity_score, excess_db)``."""

        active_before = bool(self.active)
        if candidate is None:
            self._deactivate()
            self._clear_pending()
            return CausalExitDecision(False, "REJECT_NO_DOMINANT_CANDIDATE", 0.0, 0.0, 0, active_before, False)
        azimuth, elevation, activity_score, energy_excess = (float(value) for value in candidate)
        if activity_score < float(self.config.minimum_activity_score):
            self._deactivate()
            self._clear_pending()
            return CausalExitDecision(False, "REJECT_EXIT_LOW_ACTIVITY_SCORE", activity_score, energy_excess, 0, active_before, False)
        if energy_excess < float(self.config.minimum_energy_excess_db):
            self._deactivate()
            self._clear_pending()
            return CausalExitDecision(False, "REJECT_EXIT_LOW_ENERGY_EXCESS", activity_score, energy_excess, 0, active_before, False)
        if self.active and self.azimuth_deg is not None and self.elevation_deg is not None:
            distance = angular_distance_deg(self.azimuth_deg, self.elevation_deg, azimuth, elevation)
            if distance <= float(self.config.association_radius_deg):
                self.azimuth_deg = azimuth
                self.elevation_deg = elevation
                self._clear_pending()
                return CausalExitDecision(True, "ACCEPT_ACTIVE_CONTINUATION", activity_score, energy_excess, 0, active_before, True)
            self._deactivate()
        if activity_score >= float(self.config.immediate_activity_score):
            self._activate(azimuth, elevation)
            return CausalExitDecision(True, "ACCEPT_IMMEDIATE_HIGH_ACTIVITY", activity_score, energy_excess, 0, active_before, True)
        if self.pending_azimuth_deg is not None and self.pending_elevation_deg is not None:
            distance = angular_distance_deg(self.pending_azimuth_deg, self.pending_elevation_deg, azimuth, elevation)
            if distance <= float(self.config.association_radius_deg):
                self.pending_hits += 1
                self.pending_azimuth_deg = azimuth
                self.pending_elevation_deg = elevation
            else:
                self.pending_azimuth_deg = azimuth
                self.pending_elevation_deg = elevation
                self.pending_hits = 1
        else:
            self.pending_azimuth_deg = azimuth
            self.pending_elevation_deg = elevation
            self.pending_hits = 1
        if self.pending_hits >= int(self.config.birth_hits):
            self._activate(azimuth, elevation)
            return CausalExitDecision(True, "ACCEPT_STABLE_ACTIVITY_BIRTH", activity_score, energy_excess, 0, active_before, True)
        return CausalExitDecision(False, "REJECT_PENDING_ACTIVITY_BIRTH", activity_score, energy_excess, self.pending_hits, active_before, False)
