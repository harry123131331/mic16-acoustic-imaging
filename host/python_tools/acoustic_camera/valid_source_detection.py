"""Interpretable Phase-1 valid-source detection for MIC16 spatial maps.

This module deliberately lives in the PC research branch.  It turns an SRP
map into *candidate* peaks and applies physics-guided spatial evidence before
a lightweight temporal confirmation stage.  It is not speaker recognition,
not speech classification, and not an FPGA implementation.

The public functions accept both one-dimensional azimuth spectra and
two-dimensional elevation/azimuth maps.  The current Phase-1 fixture uses the
existing two-dimensional MIC16 software SRP implementation.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, replace
from typing import Iterable

import numpy as np

from mic16_config import Mic16Config

from .srp_phat_2d import interpolate_pair_curves


VALID_SOURCE_PHASE1_VERSION = "mic16_valid_source_phase1_v1"


def angular_distance_deg(
    azimuth_a_deg: float,
    elevation_a_deg: float | None,
    azimuth_b_deg: float,
    elevation_b_deg: float | None,
) -> float:
    """Return circular 1-D or spherical 2-D angular distance in degrees."""

    if elevation_a_deg is None or elevation_b_deg is None:
        delta = abs(float(azimuth_a_deg) - float(azimuth_b_deg)) % 360.0
        return float(min(delta, 360.0 - delta))
    az_a = np.deg2rad(float(azimuth_a_deg))
    el_a = np.deg2rad(float(elevation_a_deg))
    az_b = np.deg2rad(float(azimuth_b_deg))
    el_b = np.deg2rad(float(elevation_b_deg))
    cosine = (
        np.sin(el_a) * np.sin(el_b)
        + np.cos(el_a) * np.cos(el_b) * np.cos(az_a - az_b)
    )
    return float(np.rad2deg(np.arccos(np.clip(cosine, -1.0, 1.0))))


@dataclass(frozen=True)
class CandidatePeak:
    candidate_id: int
    rank: int
    azimuth_deg: float
    elevation_deg: float | None
    response: float
    normalized_peak: float
    prominence: float
    local_contrast: float
    width_points: int
    pair_consistency: float | None = None
    pair_support_count: int = 0
    pair_count: int = 0
    accepted: bool = False
    rejection_reason: str = "NOT_EVALUATED"

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class CandidateExtraction:
    raw_local_peaks: tuple[CandidatePeak, ...]
    nms_candidates: tuple[CandidatePeak, ...]
    dynamic_range: float


@dataclass(frozen=True)
class ValidityThresholds:
    minimum_normalized_peak: float = 0.45
    minimum_prominence: float = 0.08
    minimum_pair_consistency: float = 0.56
    minimum_pair_support: int = 10


@dataclass(frozen=True)
class TemporalConfirmationConfig:
    window_frames: int = 5
    birth_hits: int = 3
    association_radius_deg: float = 15.0
    max_missed_frames: int = 2
    direction_ema_alpha: float = 0.65

    def __post_init__(self) -> None:
        if int(self.window_frames) < 1:
            raise ValueError("window_frames must be positive")
        if not 1 <= int(self.birth_hits) <= int(self.window_frames):
            raise ValueError("birth_hits must be in [1, window_frames]")
        if float(self.association_radius_deg) <= 0.0:
            raise ValueError("association_radius_deg must be positive")
        if int(self.max_missed_frames) < 0:
            raise ValueError("max_missed_frames must be non-negative")
        if not 0.0 < float(self.direction_ema_alpha) <= 1.0:
            raise ValueError("direction_ema_alpha must be in (0,1]")


@dataclass(frozen=True)
class ConfirmedSource:
    track_id: int
    azimuth_deg: float
    elevation_deg: float | None
    state: str
    age_frames: int
    hits_in_window: int
    missed_frames: int
    current_detection: bool
    candidate_id: int | None

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass
class _TemporalTrack:
    track_id: int
    azimuth_deg: float
    elevation_deg: float | None
    history: list[bool]
    age_frames: int = 1
    missed_frames: int = 0
    ever_confirmed: bool = False
    candidate_id: int | None = None


def _validate_grid(
    response: np.ndarray,
    azimuths_deg: Iterable[float] | np.ndarray,
    elevations_deg: Iterable[float] | np.ndarray | None,
) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    values = np.asarray(response, dtype=np.float32)
    azimuths = np.asarray(azimuths_deg, dtype=np.float32).reshape(-1)
    elevations = (
        None
        if elevations_deg is None
        else np.asarray(elevations_deg, dtype=np.float32).reshape(-1)
    )
    if values.ndim == 1:
        if elevations is not None:
            raise ValueError("one-dimensional response must not provide elevations")
        if values.shape[0] != azimuths.size:
            raise ValueError("azimuth grid length does not match response")
    elif values.ndim == 2:
        if elevations is None:
            raise ValueError("two-dimensional response requires elevations")
        if values.shape != (elevations.size, azimuths.size):
            raise ValueError("elevation/azimuth grid does not match response")
    else:
        raise ValueError("response must be a one- or two-dimensional spatial map")
    if values.size == 0 or not np.all(np.isfinite(values)):
        raise ValueError("response must be finite and non-empty")
    return values, azimuths, elevations


def _neighbor_values(values: np.ndarray, index: tuple[int, ...]) -> np.ndarray:
    if values.ndim == 1:
        i = int(index[0])
        indices = [(i - 1) % values.size, (i + 1) % values.size]
        return values[np.asarray(indices, dtype=np.int64)]
    elevation_index, azimuth_index = (int(index[0]), int(index[1]))
    neighbors: list[float] = []
    for de in (-1, 0, 1):
        for da in (-1, 0, 1):
            if de == 0 and da == 0:
                continue
            e = elevation_index + de
            a = azimuth_index + da
            if 0 <= e < values.shape[0] and 0 <= a < values.shape[1]:
                neighbors.append(float(values[e, a]))
    return np.asarray(neighbors, dtype=np.float32)


def _local_patch(values: np.ndarray, index: tuple[int, ...], radius: int = 2) -> np.ndarray:
    if values.ndim == 1:
        center = int(index[0])
        offsets = np.arange(-int(radius), int(radius) + 1, dtype=np.int64)
        return values[(center + offsets) % values.size]
    elevation_index, azimuth_index = (int(index[0]), int(index[1]))
    e0 = max(0, elevation_index - int(radius))
    e1 = min(values.shape[0], elevation_index + int(radius) + 1)
    a0 = max(0, azimuth_index - int(radius))
    a1 = min(values.shape[1], azimuth_index + int(radius) + 1)
    return values[e0:e1, a0:a1].reshape(-1)


def _coordinates(
    index: tuple[int, ...],
    azimuths: np.ndarray,
    elevations: np.ndarray | None,
) -> tuple[float, float | None]:
    if elevations is None:
        return float(azimuths[int(index[0])]), None
    return float(azimuths[int(index[1])]), float(elevations[int(index[0])])


def _index_for_candidate(
    candidate: CandidatePeak,
    azimuths: np.ndarray,
    elevations: np.ndarray | None,
) -> tuple[int, ...]:
    azimuth_index = int(np.argmin(np.abs(azimuths - float(candidate.azimuth_deg))))
    if elevations is None:
        return (azimuth_index,)
    elevation_index = int(
        np.argmin(np.abs(elevations - float(candidate.elevation_deg)))
    )
    return (elevation_index, azimuth_index)


def _pair_consistency(
    candidate: CandidatePeak,
    gcc_features: np.ndarray,
    lag_table: np.ndarray,
    azimuths: np.ndarray,
    elevations: np.ndarray | None,
    cfg: Mic16Config,
    pair_weights: np.ndarray | None,
) -> tuple[float, int, int]:
    curves = np.asarray(gcc_features, dtype=np.float32)
    if curves.ndim != 2:
        raise ValueError("gcc_features must have shape [pair,lag]")
    table = np.asarray(lag_table, dtype=np.float32)
    expected_shape = (
        (azimuths.size, curves.shape[0])
        if elevations is None
        else (elevations.size, azimuths.size, curves.shape[0])
    )
    if table.shape != expected_shape:
        raise ValueError(f"lag_table must have shape {expected_shape}, got {table.shape}")
    index = _index_for_candidate(candidate, azimuths, elevations)
    expected_lags = table[index]
    sampled = interpolate_pair_curves(
        curves, np.asarray(expected_lags, dtype=np.float32).reshape(1, -1), cfg
    ).reshape(-1)
    median = np.median(curves, axis=1)
    low = np.percentile(curves, 10.0, axis=1)
    high = np.percentile(curves, 90.0, axis=1)
    spread = np.maximum(high - low, 1e-8)
    standardized = (sampled - median) / spread
    soft_support = 1.0 / (1.0 + np.exp(-4.0 * (standardized - 0.10)))
    support = standardized >= 0.25
    if pair_weights is None:
        weights = np.ones(curves.shape[0], dtype=np.float64)
    else:
        weights = np.clip(
            np.asarray(pair_weights, dtype=np.float64).reshape(-1), 0.0, None
        )
        if weights.size != curves.shape[0]:
            raise ValueError("pair_weights length must equal pair count")
    active = weights > 0.0
    if not np.any(active):
        return 0.0, 0, int(curves.shape[0])
    consistency = float(np.sum(soft_support[active] * weights[active]) / np.sum(weights[active]))
    return consistency, int(np.count_nonzero(support & active)), int(np.count_nonzero(active))


def extract_candidate_peaks(
    response: np.ndarray,
    azimuths_deg: Iterable[float] | np.ndarray,
    elevations_deg: Iterable[float] | np.ndarray | None = None,
    *,
    gcc_features: np.ndarray | None = None,
    lag_table: np.ndarray | None = None,
    cfg: Mic16Config = Mic16Config(),
    pair_weights: np.ndarray | None = None,
    nms_radius_deg: float = 15.0,
    max_candidates: int = 8,
) -> CandidateExtraction:
    """Extract local maxima, attach interpretable evidence, then apply NMS."""

    values, azimuths, elevations = _validate_grid(
        response, azimuths_deg, elevations_deg
    )
    if (gcc_features is None) != (lag_table is None):
        raise ValueError("gcc_features and lag_table must be supplied together")
    dynamic_range = float(np.max(values) - np.min(values))
    if dynamic_range <= 1e-12:
        return CandidateExtraction((), (), dynamic_range)

    raw: list[CandidatePeak] = []
    for index in np.ndindex(values.shape):
        center = float(values[index])
        neighbors = _neighbor_values(values, index)
        if neighbors.size == 0:
            continue
        if center < float(np.max(neighbors)) or center <= float(np.min(neighbors)):
            continue
        patch = _local_patch(values, index, radius=2)
        background = float(np.median(patch[patch != center])) if np.any(patch != center) else float(np.min(values))
        prominence = max(0.0, (center - background) / dynamic_range)
        local_contrast = max(0.0, (center - float(np.mean(neighbors))) / dynamic_range)
        half_height = background + 0.5 * max(center - background, 0.0)
        width_points = int(np.count_nonzero(patch >= half_height))
        azimuth, elevation = _coordinates(index, azimuths, elevations)
        raw.append(
            CandidatePeak(
                candidate_id=0,
                rank=0,
                azimuth_deg=azimuth,
                elevation_deg=elevation,
                response=center,
                normalized_peak=float((center - float(np.min(values))) / dynamic_range),
                prominence=float(prominence),
                local_contrast=float(local_contrast),
                width_points=width_points,
            )
        )

    raw.sort(key=lambda item: item.response, reverse=True)
    ranked_raw = tuple(
        replace(item, candidate_id=index + 1, rank=index + 1)
        for index, item in enumerate(raw)
    )
    selected: list[CandidatePeak] = []
    for item in ranked_raw:
        if any(
            angular_distance_deg(
                item.azimuth_deg,
                item.elevation_deg,
                accepted.azimuth_deg,
                accepted.elevation_deg,
            )
            < float(nms_radius_deg)
            for accepted in selected
        ):
            continue
        candidate = replace(
            item,
            candidate_id=len(selected) + 1,
            rank=len(selected) + 1,
        )
        if gcc_features is not None and lag_table is not None:
            consistency, support_count, pair_count = _pair_consistency(
                candidate,
                gcc_features,
                lag_table,
                azimuths,
                elevations,
                cfg,
                pair_weights,
            )
            candidate = replace(
                candidate,
                pair_consistency=consistency,
                pair_support_count=support_count,
                pair_count=pair_count,
            )
        selected.append(candidate)
        if len(selected) >= int(max_candidates):
            break
    return CandidateExtraction(ranked_raw, tuple(selected), dynamic_range)


def apply_spatial_validity_gate(
    candidates: Iterable[CandidatePeak],
    thresholds: ValidityThresholds = ValidityThresholds(),
) -> tuple[CandidatePeak, ...]:
    """Apply ordered, explainable Phase-1 rejection rules."""

    evaluated: list[CandidatePeak] = []
    for candidate in candidates:
        if candidate.normalized_peak < float(thresholds.minimum_normalized_peak):
            evaluated.append(replace(candidate, accepted=False, rejection_reason="REJECT_LOW_PEAK"))
        elif candidate.prominence < float(thresholds.minimum_prominence):
            evaluated.append(replace(candidate, accepted=False, rejection_reason="REJECT_LOW_PROMINENCE"))
        elif candidate.pair_consistency is None:
            evaluated.append(replace(candidate, accepted=False, rejection_reason="REJECT_MISSING_PAIR_SUPPORT"))
        elif candidate.pair_consistency < float(thresholds.minimum_pair_consistency):
            evaluated.append(replace(candidate, accepted=False, rejection_reason="REJECT_LOW_PAIR_SUPPORT"))
        elif candidate.pair_support_count < int(thresholds.minimum_pair_support):
            evaluated.append(replace(candidate, accepted=False, rejection_reason="REJECT_TOO_FEW_SUPPORTING_PAIRS"))
        else:
            evaluated.append(replace(candidate, accepted=True, rejection_reason="ACCEPT_SPATIAL_PHASE1"))
    return tuple(evaluated)


class TemporalSourceConfirmer:
    """Nearest-neighbour M-of-N confirmation, intentionally short of Phase-4 tracking."""

    def __init__(
        self, config: TemporalConfirmationConfig = TemporalConfirmationConfig()
    ) -> None:
        self.config = config
        self._tracks: dict[int, _TemporalTrack] = {}
        self._next_track_id = 1

    def reset(self) -> None:
        self._tracks.clear()
        self._next_track_id = 1

    def update(self, candidates: Iterable[CandidatePeak]) -> tuple[ConfirmedSource, ...]:
        accepted = [candidate for candidate in candidates if candidate.accepted]
        unmatched_tracks = set(self._tracks)
        unmatched_candidates = set(range(len(accepted)))
        assignments: list[tuple[int, int]] = []
        distances: list[tuple[float, int, int]] = []
        for track_id, track in self._tracks.items():
            for candidate_index, candidate in enumerate(accepted):
                distance = angular_distance_deg(
                    track.azimuth_deg,
                    track.elevation_deg,
                    candidate.azimuth_deg,
                    candidate.elevation_deg,
                )
                if distance <= float(self.config.association_radius_deg):
                    distances.append((distance, track_id, candidate_index))
        for _, track_id, candidate_index in sorted(distances):
            if track_id not in unmatched_tracks or candidate_index not in unmatched_candidates:
                continue
            assignments.append((track_id, candidate_index))
            unmatched_tracks.remove(track_id)
            unmatched_candidates.remove(candidate_index)

        alpha = float(self.config.direction_ema_alpha)
        for track_id, candidate_index in assignments:
            track = self._tracks[track_id]
            candidate = accepted[candidate_index]
            if track.elevation_deg is None and candidate.elevation_deg is None:
                previous = np.deg2rad(track.azimuth_deg)
                current = np.deg2rad(candidate.azimuth_deg)
                sine = (1.0 - alpha) * np.sin(previous) + alpha * np.sin(current)
                cosine = (1.0 - alpha) * np.cos(previous) + alpha * np.cos(current)
                track.azimuth_deg = float(np.rad2deg(np.arctan2(sine, cosine)) % 360.0)
            else:
                track.azimuth_deg = alpha * candidate.azimuth_deg + (1.0 - alpha) * track.azimuth_deg
            if track.elevation_deg is not None and candidate.elevation_deg is not None:
                track.elevation_deg = alpha * candidate.elevation_deg + (1.0 - alpha) * track.elevation_deg
            track.history.append(True)
            track.history = track.history[-int(self.config.window_frames) :]
            track.age_frames += 1
            track.missed_frames = 0
            track.candidate_id = candidate.candidate_id

        for track_id in unmatched_tracks:
            track = self._tracks[track_id]
            track.history.append(False)
            track.history = track.history[-int(self.config.window_frames) :]
            track.age_frames += 1
            track.missed_frames += 1
            track.candidate_id = None

        for candidate_index in sorted(unmatched_candidates):
            candidate = accepted[candidate_index]
            track_id = self._next_track_id
            self._next_track_id += 1
            self._tracks[track_id] = _TemporalTrack(
                track_id=track_id,
                azimuth_deg=candidate.azimuth_deg,
                elevation_deg=candidate.elevation_deg,
                history=[True],
                candidate_id=candidate.candidate_id,
            )

        expired = [
            track_id
            for track_id, track in self._tracks.items()
            if track.missed_frames > int(self.config.max_missed_frames)
        ]
        for track_id in expired:
            del self._tracks[track_id]

        output: list[ConfirmedSource] = []
        for track_id in sorted(self._tracks):
            track = self._tracks[track_id]
            hits = int(sum(track.history))
            if hits >= int(self.config.birth_hits):
                track.ever_confirmed = True
            if track.missed_frames > 0 and track.ever_confirmed:
                state = "HOLD"
            elif track.ever_confirmed:
                state = "CONFIRMED"
            elif hits >= 2:
                state = "TENTATIVE"
            else:
                state = "CANDIDATE"
            output.append(
                ConfirmedSource(
                    track_id=track.track_id,
                    azimuth_deg=float(track.azimuth_deg),
                    elevation_deg=(
                        None if track.elevation_deg is None else float(track.elevation_deg)
                    ),
                    state=state,
                    age_frames=int(track.age_frames),
                    hits_in_window=hits,
                    missed_frames=int(track.missed_frames),
                    current_detection=track.missed_frames == 0,
                    candidate_id=track.candidate_id,
                )
            )
        return tuple(output)


def active_confirmed_sources(
    tracks: Iterable[ConfirmedSource],
) -> tuple[ConfirmedSource, ...]:
    """Return confirmed tracks supported by a spatial detection this frame."""

    return tuple(
        track
        for track in tracks
        if track.state == "CONFIRMED" and track.current_detection
    )
