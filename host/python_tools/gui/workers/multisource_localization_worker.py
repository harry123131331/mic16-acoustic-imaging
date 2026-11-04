"""Frozen TF-zone + event-dynamics adapter for the MIC16 GUI.

This worker consumes one 20-frame window and a separately calibrated noise
history.  It performs no tracking, speaker identification, threshold tuning,
or visual rendering.  The GUI is free to change style without changing this
algorithm/contract boundary.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import numpy as np

from acoustic_camera.candidate_event_dynamics import (
    CandidateEventDynamicsThresholds,
    decide_candidate_event_dynamics,
    extract_candidate_event_dynamics,
)
from acoustic_camera.candidate_speech_activity import delay_and_sum_candidate_azel
from acoustic_camera.tf_zone_histogram_2d import (
    TfZoneAnalysisConfig,
    TfZoneProjector,
    TfZoneThresholds,
    select_tf_zone_candidates,
)
from gui.multisource_contract import (
    EffectiveSource2D,
    MultisourceLocalizationResult,
    SpatialCandidate2D,
    validate_multisource_payload,
)
from mic16_config import Mic16Config


ALGORITHM_ID = "tf_zone_histogram_azel_dev1+candidate_event_dynamics_dev1"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class TfZoneSemanticMultisourceAdapter:
    """Hash-checked software adapter for one anonymous multi-source window."""

    def __init__(self, project_root: Path | None = None) -> None:
        self.root = Path(project_root) if project_root else Path(__file__).resolve().parents[3]
        self.cfg = Mic16Config()
        self.frame_count = 20
        self.spatial_dir = self.root / "results/tf_zone_histogram_dev_v1"
        self.semantic_dir = self.root / "results/tf_zone_semantic_gate_dev_v1"
        self.hold_dir = self.root / "results/tf_zone_semantic_fresh_hold_v1"
        self._load_frozen_contract()
        self.projector = TfZoneProjector(self.cfg, config=TfZoneAnalysisConfig())
        self._noise_frames: np.ndarray | None = None
        self._calibration_id = ""

    def _load_frozen_contract(self) -> None:
        selected_path = self.spatial_dir / "selected_thresholds.json"
        gate_path = self.semantic_dir / "frozen_gate.json"
        hold_protocol_path = self.hold_dir / "protocol_lock.json"
        hold_summary_path = self.hold_dir / "summary.json"
        for path in (selected_path, gate_path, hold_protocol_path, hold_summary_path):
            if not path.is_file():
                raise FileNotFoundError(f"missing frozen multisource evidence: {path}")
        selected = json.loads(selected_path.read_text(encoding="utf-8"))
        gate = json.loads(gate_path.read_text(encoding="utf-8"))
        protocol = json.loads(hold_protocol_path.read_text(encoding="utf-8"))
        summary = json.loads(hold_summary_path.read_text(encoding="utf-8"))
        if summary.get("status") != "PASS_FRESH_HOLD" or summary.get("passed") is not True:
            raise RuntimeError("multisource GUI adapter requires a passed frozen HOLD")
        expected_hashes = {
            selected_path: protocol["frozen_spatial_thresholds_sha256"],
            gate_path: protocol["frozen_semantic_gate_sha256"],
            hold_protocol_path: summary["protocol_sha256"],
        }
        for path, expected in expected_hashes.items():
            if _sha256(path) != expected:
                raise RuntimeError(f"frozen multisource evidence hash mismatch: {path}")
        self.spatial_thresholds = TfZoneThresholds(**selected["thresholds"])
        self.event_thresholds = CandidateEventDynamicsThresholds(**gate["thresholds"])
        self.protocol_sha256 = summary["protocol_sha256"]

    @property
    def status(self) -> str:
        calibration = self._calibration_id or "required"
        return f"{ALGORITHM_ID}; HOLD=PASS; noise_calibration={calibration}"

    def calibrate_noise(self, noise_frames: np.ndarray, calibration_id: str) -> None:
        values = self._validate_frames(noise_frames, minimum=4)
        calibration_id = str(calibration_id).strip()
        if not calibration_id:
            raise ValueError("calibration_id is required")
        self._noise_frames = values.copy()
        self._calibration_id = calibration_id

    def clear_noise_calibration(self) -> None:
        self._noise_frames = None
        self._calibration_id = ""

    def process_window(self, frames: np.ndarray, window_id: str) -> MultisourceLocalizationResult:
        if self._noise_frames is None:
            raise RuntimeError("noise calibration is required before multi-source localization")
        window_id = str(window_id).strip()
        if not window_id:
            raise ValueError("window_id is required")
        values = self._validate_frames(frames, exact=self.frame_count)
        votes = self.projector.extract_votes(values, self._noise_frames)
        spatial = select_tf_zone_candidates(votes, self.spatial_thresholds)
        candidates: list[SpatialCandidate2D] = []
        sources: list[EffectiveSource2D] = []
        for candidate in spatial:
            beam_frames = np.stack(
                [
                    delay_and_sum_candidate_azel(
                        frame,
                        float(candidate.azimuth_deg),
                        float(candidate.elevation_deg),
                        self.cfg,
                    )
                    for frame in values
                ]
            )
            features = extract_candidate_event_dynamics(beam_frames, self.cfg.sample_rate)
            decision = decide_candidate_event_dynamics(features, self.event_thresholds)
            level_margin = float(features.level_std_db - self.event_thresholds.minimum_level_std_db)
            entropy_margin = float(
                features.spectral_entropy_std - self.event_thresholds.minimum_spectral_entropy_std
            )
            candidates.append(
                SpatialCandidate2D(
                    rank=int(candidate.rank),
                    azimuth_deg=float(candidate.azimuth_deg),
                    elevation_deg=float(candidate.elevation_deg),
                    accepted=bool(decision.accepted),
                    decision_reason=decision.reason,
                    vote_count=int(candidate.vote_count),
                    frame_support=int(candidate.frame_support),
                    coarse_band_support=int(candidate.coarse_band_support),
                    zone_support=int(candidate.zone_support),
                    mean_spatial_coherence=float(candidate.mean_coherence),
                    mean_energy_excess_db=float(candidate.mean_energy_excess_db),
                    level_std_db=float(features.level_std_db),
                    spectral_entropy_std=float(features.spectral_entropy_std),
                    level_gate_margin_db=level_margin,
                    entropy_gate_margin=entropy_margin,
                )
            )
            if decision.accepted:
                sources.append(
                    EffectiveSource2D(
                        source_id=f"{window_id}:candidate:{int(candidate.rank)}",
                        candidate_rank=int(candidate.rank),
                        azimuth_deg=float(candidate.azimuth_deg),
                        elevation_deg=float(candidate.elevation_deg),
                        anonymous=True,
                        evidence_kind="frozen_event_dynamics_gate_margin",
                        level_gate_margin_db=level_margin,
                        entropy_gate_margin=entropy_margin,
                    )
                )
        result = MultisourceLocalizationResult(
            window_id=window_id,
            status="OK" if sources else "NO_VALID_SOURCE",
            algorithm=ALGORITHM_ID,
            calibration_id=self._calibration_id,
            frame_count=self.frame_count,
            history_seconds=self.frame_count * self.cfg.frame_samples / self.cfg.sample_rate,
            sources=tuple(sources),
            spatial_candidates=tuple(candidates),
        )
        validate_multisource_payload(result.to_payload())
        return result

    def _validate_frames(
        self,
        frames: np.ndarray,
        *,
        minimum: int | None = None,
        exact: int | None = None,
    ) -> np.ndarray:
        values = np.asarray(frames)
        expected_tail = (int(self.cfg.frame_samples), int(self.cfg.channels))
        if values.ndim != 3 or values.shape[1:] != expected_tail:
            raise ValueError(f"frames must have shape [N,{expected_tail[0]},{expected_tail[1]}]")
        if exact is not None and values.shape[0] != exact:
            raise ValueError(f"exactly {exact} frames are required")
        if minimum is not None and values.shape[0] < minimum:
            raise ValueError(f"at least {minimum} frames are required")
        if not np.isfinite(values).all():
            raise ValueError("frames must be finite")
        return values.astype(np.float32, copy=False)

