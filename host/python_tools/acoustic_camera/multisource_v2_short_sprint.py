"""Bounded v2 structures for the final MIC16 multi-source short sprint.

The module deliberately contains only two truth-free operations:

* rescue a candidate whose frozen v1 event gate narrowly fails but whose
  temporal range and periodicity dynamics remain speech-like;
* suppress a lower-ranked accepted candidate when its normalized time-
  frequency dynamics are nearly identical to an already retained beam.

Neither operation moves a direction, creates a spatial candidate, estimates
speaker identity, or reads source-count/direction truth.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Protocol, Sequence

import numpy as np

from .candidate_event_dynamics import CandidateEventDynamicsFeatures
from .valid_source_detection import angular_distance_deg


MULTISOURCE_V2_SHORT_SPRINT_VERSION = "mic16_multisource_v2_short_sprint_preregistered_v1"


class DirectionCandidate(Protocol):
    azimuth_deg: float
    elevation_deg: float
    rank: int


@dataclass(frozen=True)
class DynamicRescueThresholds:
    minimum_level_std_db: float = 2.00
    minimum_level_p90_p10_db: float = 6.00
    minimum_periodicity_std: float = 0.090
    minimum_entropy_std_alternative: float = 0.070

    def to_dict(self) -> dict[str, float]:
        return asdict(self)


@dataclass(frozen=True)
class DuplicateSuppressionThresholds:
    minimum_fingerprint_similarity: float = 0.965
    frequency_min_hz: float = 300.0
    frequency_max_hz: float = 8000.0
    band_count: int = 12

    def to_dict(self) -> dict[str, float | int]:
        return asdict(self)


def dynamic_rescue_accept(
    features: CandidateEventDynamicsFeatures,
    thresholds: DynamicRescueThresholds = DynamicRescueThresholds(),
) -> bool:
    """Return the fixed alternate speech-like dynamics decision.

    The thresholds are rounded inside the separation observed on the original
    TF-zone TRAIN features.  This function is an OR branch after the frozen v1
    decision; it never rejects a candidate accepted by v1.
    """

    return bool(
        float(features.level_std_db) >= float(thresholds.minimum_level_std_db)
        and float(features.level_p90_p10_db)
        >= float(thresholds.minimum_level_p90_p10_db)
        and float(features.periodicity_std) >= float(thresholds.minimum_periodicity_std)
        and (
            float(features.level_std_db) >= 2.30
            or float(features.spectral_entropy_std)
            >= float(thresholds.minimum_entropy_std_alternative)
        )
    )


def beam_tf_dynamics_fingerprint(
    beam_frames: np.ndarray,
    sample_rate: float,
    thresholds: DuplicateSuppressionThresholds = DuplicateSuppressionThresholds(),
) -> np.ndarray:
    """Return a gain-invariant temporal log-band fingerprint for one beam."""

    values = np.asarray(beam_frames, dtype=np.float64)
    if values.ndim != 2 or values.shape[0] < 4 or values.shape[1] < 64:
        raise ValueError("beam_frames must have shape [frame>=4,sample>=64]")
    if not np.isfinite(values).all() or float(sample_rate) <= 0.0:
        raise ValueError("beam_frames and sample_rate must be finite and valid")
    window = np.hanning(values.shape[1])[None, :]
    spectra = np.fft.rfft(values * window, axis=1)
    power = np.square(np.abs(spectra))
    frequencies = np.fft.rfftfreq(values.shape[1], d=1.0 / float(sample_rate))
    edges = np.geomspace(
        float(thresholds.frequency_min_hz),
        float(thresholds.frequency_max_hz),
        int(thresholds.band_count) + 1,
    )
    bands = []
    for index in range(int(thresholds.band_count)):
        include_upper = index == int(thresholds.band_count) - 1
        mask = (frequencies >= edges[index]) & (
            frequencies <= edges[index + 1] if include_upper else frequencies < edges[index + 1]
        )
        if not np.any(mask):
            raise ValueError("fingerprint band contains no FFT bins")
        bands.append(10.0 * np.log10(np.maximum(np.mean(power[:, mask], axis=1), 1e-20)))
    log_bands = np.stack(bands, axis=1)
    centered = log_bands - np.mean(log_bands, axis=0, keepdims=True)
    scale = np.std(centered, axis=0, keepdims=True)
    normalized = np.divide(centered, scale, out=np.zeros_like(centered), where=scale > 1e-8)
    fingerprint = normalized.reshape(-1)
    norm = float(np.linalg.norm(fingerprint))
    if norm <= 1e-10:
        return np.zeros_like(fingerprint)
    return (fingerprint / norm).astype(np.float64)


def fingerprint_similarity(left: np.ndarray, right: np.ndarray) -> float:
    left_values = np.asarray(left, dtype=np.float64).reshape(-1)
    right_values = np.asarray(right, dtype=np.float64).reshape(-1)
    if left_values.shape != right_values.shape or left_values.size == 0:
        raise ValueError("fingerprints must have the same non-empty shape")
    return float(np.clip(np.dot(left_values, right_values), -1.0, 1.0))


def suppress_duplicate_candidates(
    candidates: Sequence[DirectionCandidate],
    beam_frames: Sequence[np.ndarray],
    initially_accepted: Sequence[bool],
    sample_rate: float,
    thresholds: DuplicateSuppressionThresholds = DuplicateSuppressionThresholds(),
) -> tuple[tuple[bool, ...], tuple[dict[str, float | int], ...]]:
    """Suppress a lower-ranked beam with a near-identical TF-dynamics trace."""

    if not (len(candidates) == len(beam_frames) == len(initially_accepted)):
        raise ValueError("candidate, beam, and acceptance lengths must match")
    fingerprints = [
        beam_tf_dynamics_fingerprint(values, sample_rate, thresholds) for values in beam_frames
    ]
    accepted = [bool(value) for value in initially_accepted]
    kept: list[int] = []
    diagnostics: list[dict[str, float | int]] = []
    order = sorted(range(len(candidates)), key=lambda index: int(candidates[index].rank))
    for candidate_index in order:
        if not accepted[candidate_index]:
            continue
        duplicate_of: int | None = None
        duplicate_similarity = -1.0
        for kept_index in kept:
            similarity = fingerprint_similarity(
                fingerprints[candidate_index], fingerprints[kept_index]
            )
            if similarity > duplicate_similarity:
                duplicate_similarity = similarity
                duplicate_of = kept_index
        if (
            duplicate_of is not None
            and duplicate_similarity >= float(thresholds.minimum_fingerprint_similarity)
        ):
            accepted[candidate_index] = False
            diagnostics.append(
                {
                    "kept_rank": int(candidates[duplicate_of].rank),
                    "suppressed_rank": int(candidates[candidate_index].rank),
                    "fingerprint_similarity": float(duplicate_similarity),
                    "angular_separation_deg": float(
                        angular_distance_deg(
                            float(candidates[duplicate_of].azimuth_deg),
                            float(candidates[duplicate_of].elevation_deg),
                            float(candidates[candidate_index].azimuth_deg),
                            float(candidates[candidate_index].elevation_deg),
                        )
                    ),
                }
            )
        else:
            kept.append(candidate_index)
    return tuple(accepted), tuple(diagnostics)

