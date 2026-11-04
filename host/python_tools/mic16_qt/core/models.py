from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass(frozen=True)
class SourceEstimate:
    source_id: str
    azimuth_deg: float
    elevation_deg: float
    evidence_db: float
    state: str = "TRACK"
    evidence_text: str | None = None
    score: float | None = None


@dataclass(frozen=True)
class CandidateEstimate:
    azimuth_deg: float
    elevation_deg: float
    evidence_db: float
    accepted: bool


@dataclass(frozen=True)
class AcousticFrame:
    frame_index: int
    sample_rate_hz: float
    pcm: np.ndarray
    enhanced: np.ndarray
    frequency_hz: np.ndarray
    raw_spectrum_db: np.ndarray
    enhanced_spectrum_db: np.ndarray
    azimuth_axis_deg: np.ndarray
    elevation_axis_deg: np.ndarray
    response_db: np.ndarray
    sources: tuple[SourceEstimate, ...]
    candidates: tuple[CandidateEstimate, ...]
    packet_loss: int
    latency_ms: float
    input_rms_dbfs: float
    output_rms_dbfs: float
    snr_gain_db: float
    algorithm_name: str
    evidence_source: str
    raw_pcm_i32: np.ndarray | None = None

    def validate(self) -> None:
        if self.pcm.ndim != 2 or self.pcm.shape[1] != 16:
            raise ValueError("pcm must have shape [samples, 16]")
        if self.enhanced.ndim != 1 or self.enhanced.shape[0] != self.pcm.shape[0]:
            raise ValueError("enhanced must align with pcm")
        if self.response_db.shape != (
            self.elevation_axis_deg.size,
            self.azimuth_axis_deg.size,
        ):
            raise ValueError("response grid axes do not match response_db")
        if not np.isfinite(self.pcm).all() or not np.isfinite(self.response_db).all():
            raise ValueError("frame contains non-finite data")
        if self.raw_pcm_i32 is not None:
            if self.raw_pcm_i32.shape != self.pcm.shape or self.raw_pcm_i32.dtype != np.int32:
                raise ValueError("raw_pcm_i32 must be int32 and align with pcm")
        if self.evidence_source not in {"BOARD LIVE", "PC LIVE", "REPLAY", "SYNTHETIC"}:
            raise ValueError("unsupported evidence source")


@dataclass(frozen=True)
class LocalizationUpdate:
    frame_index: int
    sources: tuple[SourceEstimate, ...]
    candidate_count: int
    latency_ms: float
    algorithm_name: str
    calibration_crc32: int
    flags: int
    evidence_source: str = "BOARD LIVE"

    def validate(self) -> None:
        if self.frame_index < 0 or self.candidate_count < len(self.sources):
            raise ValueError("invalid localization counters")
        for source in self.sources:
            if not -180.0 <= source.azimuth_deg <= 180.0:
                raise ValueError("azimuth is out of range")
            if not -90.0 <= source.elevation_deg <= 90.0:
                raise ValueError("elevation is out of range")
        if self.evidence_source != "BOARD LIVE":
            raise ValueError("protocol localization must be BOARD LIVE evidence")


@dataclass(frozen=True)
class EnhancedAudioUpdate:
    frame_index: int
    sample_rate_hz: float
    raw_samples_i32: np.ndarray
    samples: np.ndarray
    frequency_hz: np.ndarray
    spectrum_db: np.ndarray
    output_rms_dbfs: float
    evidence_source: str = "BOARD LIVE"

    def validate(self) -> None:
        if self.raw_samples_i32.ndim != 1 or self.raw_samples_i32.dtype != np.int32:
            raise ValueError("enhanced raw samples must be mono int32")
        if self.samples.ndim != 1 or self.samples.shape != self.raw_samples_i32.shape:
            raise ValueError("normalized enhanced samples must align with raw samples")
        if self.frequency_hz.shape != self.spectrum_db.shape:
            raise ValueError("enhanced spectrum axis mismatch")
        if not np.isfinite(self.samples).all() or not np.isfinite(self.spectrum_db).all():
            raise ValueError("enhanced update contains non-finite data")
