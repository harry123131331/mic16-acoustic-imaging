"""Explicit truth semantics for Phase-3/DEV-5 acoustic scenes.

This module deliberately rejects unknown scene kinds.  Earlier experiments
implicitly treated every unrecognised kind as speech, which allowed renamed
procedural-noise scenes to acquire positive DOA truth labels.
"""

from __future__ import annotations

import math
from typing import Final


NEGATIVE_SCENE_KINDS: Final[frozenset[str]] = frozenset(
    {
        "directional_office_noise",
        "multichannel_office_noise",
        "directional_procedural_noise",
        "multichannel_procedural_noise",
    }
)

CONTINUOUS_SPEECH_SCENE_KINDS: Final[frozenset[str]] = frozenset(
    {
        "natural_speech_rir",
        "natural_speech_rir_office",
    }
)

STOP_TAIL_SCENE_KIND: Final[str] = "natural_speech_stop_tail"

KNOWN_SCENE_KINDS: Final[frozenset[str]] = frozenset(
    set(NEGATIVE_SCENE_KINDS)
    | set(CONTINUOUS_SPEECH_SCENE_KINDS)
    | {STOP_TAIL_SCENE_KIND}
)


def scene_has_active_speech(kind: str, frame_index: int, *, stop_frame: int = 3) -> bool:
    """Return whether a frame has speech truth under the explicit contract."""

    if kind not in KNOWN_SCENE_KINDS:
        raise ValueError(f"unknown scene kind: {kind!r}")
    if frame_index < 0:
        raise ValueError("frame_index must be non-negative")
    if stop_frame < 0:
        raise ValueError("stop_frame must be non-negative")
    if kind in NEGATIVE_SCENE_KINDS:
        return False
    if kind == STOP_TAIL_SCENE_KIND:
        return frame_index < stop_frame
    return True


def truths_for_scene(
    kind: str,
    frame_index: int,
    truth_azimuth_deg: float,
    truth_elevation_deg: float,
    *,
    stop_frame: int = 3,
) -> tuple[tuple[float, float], ...]:
    """Return zero or one active-source direction for a single-source scene."""

    if not scene_has_active_speech(kind, frame_index, stop_frame=stop_frame):
        return ()
    azimuth = float(truth_azimuth_deg)
    elevation = float(truth_elevation_deg)
    if not math.isfinite(azimuth) or not math.isfinite(elevation):
        raise ValueError("active-speech truth angles must be finite")
    return ((azimuth, elevation),)
