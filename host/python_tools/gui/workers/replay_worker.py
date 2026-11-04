# -*- coding: utf-8 -*-
"""Replay helpers for raw_i32 recordings saved by the MIC16 Python tools."""

from __future__ import annotations

import csv
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass(frozen=True)
class ReplaySegment:
    segment_id: int
    sample_start: int
    sample_end: int

    @property
    def sample_count(self) -> int:
        return max(0, int(self.sample_end) - int(self.sample_start))


def _segments_from_csv(path: Path) -> list[ReplaySegment]:
    segments: dict[int, list[int]] = {}
    with path.open("r", newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                seg_id = int(row.get("segment_id", 0))
                start = int(float(row.get("sample_start", 0)))
                end = int(float(row.get("sample_end", start)))
            except ValueError:
                continue
            if seg_id not in segments:
                segments[seg_id] = [start, end]
            else:
                segments[seg_id][0] = min(segments[seg_id][0], start)
                segments[seg_id][1] = max(segments[seg_id][1], end)
    return [ReplaySegment(k, v[0], v[1]) for k, v in sorted(segments.items()) if v[1] > v[0]]


def _segments_from_json(path: Path) -> list[ReplaySegment]:
    data = json.loads(path.read_text(encoding="utf-8"))
    rows = data.get("segments", data if isinstance(data, list) else [])
    segments: list[ReplaySegment] = []
    for idx, row in enumerate(rows):
        try:
            segments.append(
                ReplaySegment(
                    int(row.get("segment_id", idx)),
                    int(row.get("sample_start", row.get("start", 0))),
                    int(row.get("sample_end", row.get("end", 0))),
                )
            )
        except (AttributeError, ValueError, TypeError):
            continue
    return [seg for seg in segments if seg.sample_count > 0]


def find_manifest_segments(recording_dir: Path) -> list[ReplaySegment]:
    candidates = [
        recording_dir / "packet_manifest.csv",
        recording_dir / "packet_manifest.json",
        recording_dir / "segments.json",
    ]
    for path in candidates:
        if not path.exists():
            continue
        try:
            if path.suffix.lower() == ".csv":
                segments = _segments_from_csv(path)
            else:
                segments = _segments_from_json(path)
            if segments:
                return segments
        except Exception:
            continue
    return []


def load_raw_i32(path: Path, channels: int = 16) -> np.ndarray:
    raw = np.load(str(path))
    if raw.ndim == 1:
        if raw.size % int(channels) != 0:
            raise ValueError(f"flat raw_i32 length {raw.size} is not divisible by {channels}")
        raw = raw.reshape(-1, int(channels))
    if raw.ndim != 2 or raw.shape[1] < int(channels):
        raise ValueError(f"raw_i32 must have shape [N,{channels}], got {raw.shape}")
    return raw[:, : int(channels)].astype(np.int32, copy=False)


def select_longest_segment(raw: np.ndarray, segments: list[ReplaySegment] | None) -> ReplaySegment:
    if not segments:
        return ReplaySegment(0, 0, int(raw.shape[0]))
    bounded = [
        ReplaySegment(seg.segment_id, max(0, seg.sample_start), min(int(raw.shape[0]), seg.sample_end))
        for seg in segments
    ]
    bounded = [seg for seg in bounded if seg.sample_count > 0]
    if not bounded:
        return ReplaySegment(0, 0, int(raw.shape[0]))
    return max(bounded, key=lambda seg: seg.sample_count)


def iter_frames(raw: np.ndarray, segment: ReplaySegment, frame_len: int = 2048, hop: int = 2048):
    start = int(segment.sample_start)
    end = int(segment.sample_end)
    for pos in range(start, max(start, end - int(frame_len) + 1), int(hop)):
        yield pos, raw[pos : pos + int(frame_len), :].astype(np.float32)

