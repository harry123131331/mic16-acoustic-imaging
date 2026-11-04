"""Causal low-SNR admission and peak-drop release for one dominant source."""

from __future__ import annotations

from dataclasses import asdict, dataclass

from .valid_source_detection import angular_distance_deg


CAUSAL_ACTIVITY_RELEASE_VERSION = "mic16_causal_activity_release_v1"


@dataclass(frozen=True)
class CausalActivityReleaseConfig:
    minimum_activity_score: float = 0.10
    minimum_energy_excess_db: float = -2.0
    immediate_activity_score: float = 0.35
    birth_hits: int = 2
    association_radius_deg: float = 20.0
    release_peak_drop_db: float = 5.0
    release_score_drop: float = 0.05
    release_hits: int = 1

    def __post_init__(self) -> None:
        for name in (
            "minimum_activity_score",
            "immediate_activity_score",
            "release_score_drop",
        ):
            value = float(getattr(self, name))
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} must be in [0,1]")
        if float(self.immediate_activity_score) < float(self.minimum_activity_score):
            raise ValueError("immediate_activity_score must be >= minimum_activity_score")
        if int(self.birth_hits) < 1 or int(self.release_hits) < 1:
            raise ValueError("birth_hits and release_hits must be positive")
        if float(self.association_radius_deg) <= 0.0:
            raise ValueError("association_radius_deg must be positive")
        if float(self.release_peak_drop_db) < 0.0:
            raise ValueError("release_peak_drop_db must be non-negative")

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class CausalActivityReleaseDecision:
    accepted: bool
    reason: str
    pending_birth_hits: int
    pending_release_hits: int
    peak_activity_score: float
    peak_energy_excess_db: float


class CausalActivityReleaseGate:
    """Admit low-SNR candidates causally and release on sustained peak decay."""

    def __init__(self, config: CausalActivityReleaseConfig = CausalActivityReleaseConfig()) -> None:
        self.config = config
        self.active = False
        self.azimuth_deg: float | None = None
        self.elevation_deg: float | None = None
        self.pending_azimuth_deg: float | None = None
        self.pending_elevation_deg: float | None = None
        self.pending_birth_hits = 0
        self.pending_release_hits = 0
        self.peak_activity_score = 0.0
        self.peak_energy_excess_db = float("-inf")

    def _clear_birth(self) -> None:
        self.pending_azimuth_deg = None
        self.pending_elevation_deg = None
        self.pending_birth_hits = 0

    def _deactivate(self) -> None:
        self.active = False
        self.azimuth_deg = None
        self.elevation_deg = None
        self.pending_release_hits = 0
        self.peak_activity_score = 0.0
        self.peak_energy_excess_db = float("-inf")

    def _activate(self, azimuth: float, elevation: float, score: float, excess: float) -> None:
        self.active = True
        self.azimuth_deg = azimuth
        self.elevation_deg = elevation
        self.peak_activity_score = score
        self.peak_energy_excess_db = excess
        self.pending_release_hits = 0
        self._clear_birth()

    def _decision(self, accepted: bool, reason: str) -> CausalActivityReleaseDecision:
        return CausalActivityReleaseDecision(
            accepted,
            reason,
            self.pending_birth_hits,
            self.pending_release_hits,
            self.peak_activity_score,
            self.peak_energy_excess_db,
        )

    def update(self, candidate: tuple[float, float, float, float] | None) -> CausalActivityReleaseDecision:
        """Update with ``(azimuth, elevation, activity_score, excess_db)``."""

        if candidate is None:
            self._deactivate()
            self._clear_birth()
            return self._decision(False, "REJECT_NO_DOMINANT_CANDIDATE")
        azimuth, elevation, score, excess = (float(value) for value in candidate)
        if score < float(self.config.minimum_activity_score):
            self._deactivate()
            self._clear_birth()
            return self._decision(False, "REJECT_LOW_ACTIVITY")
        if excess < float(self.config.minimum_energy_excess_db):
            self._deactivate()
            self._clear_birth()
            return self._decision(False, "REJECT_LOW_ENERGY_EXCESS")

        if self.active and self.azimuth_deg is not None and self.elevation_deg is not None:
            distance = angular_distance_deg(self.azimuth_deg, self.elevation_deg, azimuth, elevation)
            if distance <= float(self.config.association_radius_deg):
                energy_drop = self.peak_energy_excess_db - excess
                score_drop = self.peak_activity_score - score
                if (
                    energy_drop >= float(self.config.release_peak_drop_db)
                    and score_drop >= float(self.config.release_score_drop)
                ):
                    self.pending_release_hits += 1
                    if self.pending_release_hits >= int(self.config.release_hits):
                        self._deactivate()
                        self._clear_birth()
                        return self._decision(False, "REJECT_PEAK_DECAY_RELEASE")
                    return self._decision(True, "ACCEPT_RELEASE_PENDING")
                self.pending_release_hits = 0
                self.azimuth_deg = azimuth
                self.elevation_deg = elevation
                self.peak_activity_score = max(self.peak_activity_score, score)
                self.peak_energy_excess_db = max(self.peak_energy_excess_db, excess)
                return self._decision(True, "ACCEPT_ACTIVE_CONTINUATION")
            self._deactivate()

        if score >= float(self.config.immediate_activity_score):
            self._activate(azimuth, elevation, score, excess)
            return self._decision(True, "ACCEPT_IMMEDIATE_ACTIVITY")

        if self.pending_azimuth_deg is not None and self.pending_elevation_deg is not None:
            distance = angular_distance_deg(
                self.pending_azimuth_deg,
                self.pending_elevation_deg,
                azimuth,
                elevation,
            )
            if distance <= float(self.config.association_radius_deg):
                self.pending_birth_hits += 1
            else:
                self.pending_birth_hits = 1
            self.pending_azimuth_deg = azimuth
            self.pending_elevation_deg = elevation
        else:
            self.pending_azimuth_deg = azimuth
            self.pending_elevation_deg = elevation
            self.pending_birth_hits = 1
        if self.pending_birth_hits >= int(self.config.birth_hits):
            self._activate(azimuth, elevation, score, excess)
            return self._decision(True, "ACCEPT_STABLE_ACTIVITY_BIRTH")
        return self._decision(False, "REJECT_PENDING_ACTIVITY_BIRTH")
