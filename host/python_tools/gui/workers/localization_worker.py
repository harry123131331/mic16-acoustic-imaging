# -*- coding: utf-8 -*-
"""Formal Python localization adapter used by the MIC16 GUI.

The adapter keeps GUI code out of the signal-processing path.  It implements
the Python reference path:

PCM frame -> GCC-PHAT -> CA-QW-SRP-PHAT -> 5-frame CCTCN -> angle result.
"""

from __future__ import annotations

import json
import math
import sys
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from mic16_config import (
    OFFICIAL_ARRAY_RADIUS_M,
    OFFICIAL_CHANNELS,
    OFFICIAL_FFT_SIZE,
    OFFICIAL_FRAME_SAMPLES,
    OFFICIAL_MAX_LAG,
    OFFICIAL_PAIR_COUNT,
    OFFICIAL_PHAT_EPSILON,
    OFFICIAL_SAMPLE_RATE,
)


def _metadata_dict(value: Any) -> dict[str, Any]:
    if isinstance(value, dict):
        return value
    if isinstance(value, str) and value.strip():
        parsed = json.loads(value)
        if isinstance(parsed, dict):
            return parsed
    return {}


@dataclass
class ProposedLocalizationResult:
    method: str
    frame: int
    angle: float
    confidence: float
    distance: float | None
    top3_classes: list[int]
    top3_angles_deg: list[float]
    entropy: float
    confidence_head_raw: float | None
    srp_peak_margin: float
    mean_pair_reliability: float
    valid_pair_count: int
    warmup: bool
    frame_valid: bool
    invalid_reason: str
    latency_ms: float = 0.0

    def to_payload(self) -> dict[str, Any]:
        return {
            "method": self.method,
            "frame": self.frame,
            "angle": self.angle,
            "confidence": self.confidence,
            "distance": self.distance,
            "top3_classes": self.top3_classes,
            "top3_angles_deg": self.top3_angles_deg,
            "entropy": self.entropy,
            "confidence_head_raw": self.confidence_head_raw,
            "srp_peak_margin": self.srp_peak_margin,
            "mean_pair_reliability": self.mean_pair_reliability,
            "valid_pair_count": self.valid_pair_count,
            "warmup": self.warmup,
            "frame_valid": self.frame_valid,
            "invalid_reason": self.invalid_reason,
            "latency_ms": self.latency_ms,
        }


class CaQwSrpCctcnAdapter:
    """Lazy-loaded CA-QW-SRP-CCTCN Python reference adapter."""

    def __init__(
        self,
        ssl_nn_dir: Path,
        checkpoint_path: Path,
        calibration_path: Path | None = None,
        sequence_len: int = 5,
        confidence_threshold: float = 0.7880566716194153,
        min_valid_pairs: int = 20,
        allow_identity_calibration: bool = False,
    ) -> None:
        self.ssl_nn_dir = Path(ssl_nn_dir)
        self.checkpoint_path = Path(checkpoint_path)
        self.calibration_path = Path(calibration_path) if calibration_path else None
        self.sequence_len = int(sequence_len)
        self.confidence_threshold = float(confidence_threshold)
        self.min_valid_pairs = int(min_valid_pairs)
        self.allow_identity_calibration = bool(allow_identity_calibration)
        self.loaded = False
        self.status = "not loaded"
        self._seq: deque[tuple[np.ndarray, np.ndarray, np.ndarray]] = deque(maxlen=self.sequence_len)

    def reset_sequence(self) -> None:
        self._seq.clear()

    @staticmethod
    def _validate_checkpoint_contract(ckpt: dict[str, Any], shape: Any) -> None:
        from pair_quality import GLOBAL_QUALITY_FIELDS, PairQualityConfig
        from quality_weighted_srp_phat import CA_QW_FORMULA_VERSION, SRP_PHAT_FORMULA_VERSION

        issues: list[str] = []
        meta = _metadata_dict(ckpt.get("meta", {}))
        dataset_meta = _metadata_dict(meta.get("dataset_metadata", {}))
        source_meta = _metadata_dict(dataset_meta.get("source_metadata", {}))

        required = {
            "sample_rate": OFFICIAL_SAMPLE_RATE,
            "channels": OFFICIAL_CHANNELS,
            "frame_len": OFFICIAL_FRAME_SAMPLES,
            "fft_len": OFFICIAL_FFT_SIZE,
            "array_radius_m": OFFICIAL_ARRAY_RADIUS_M,
        }
        for key, expected in required.items():
            if key not in source_meta:
                issues.append(f"missing source metadata: {key}")
                continue
            actual = source_meta[key]
            if isinstance(expected, float):
                if abs(float(actual) - expected) > 1e-6:
                    issues.append(f"{key}: expected {expected}, got {actual}")
            elif int(actual) != expected:
                issues.append(f"{key}: expected {expected}, got {actual}")

        if str(meta.get("feature_frontend", "")) != "gcc_phat":
            issues.append("feature_frontend must be gcc_phat")
        if str(source_meta.get("feature_type", "")) != "ca_qw_srp_phat":
            issues.append("source feature_type must be ca_qw_srp_phat")
        if str(source_meta.get("feature_formula_version", "")) != CA_QW_FORMULA_VERSION:
            issues.append(
                f"feature_formula_version must be {CA_QW_FORMULA_VERSION}"
            )
        if str(source_meta.get("srp_formula_version", "")) != SRP_PHAT_FORMULA_VERSION:
            issues.append(f"srp_formula_version must be {SRP_PHAT_FORMULA_VERSION}")
        if list(source_meta.get("input_shape", [])) != [OFFICIAL_PAIR_COUNT, 2 * OFFICIAL_MAX_LAG + 1]:
            issues.append("GCC input_shape must be [32,41]")
        if str(source_meta.get("lag_order", "")) != "ascending_-max_to_+max":
            issues.append("lag_order must be ascending_-max_to_+max")
        if int(dataset_meta.get("sequence_len", -1)) != int(shape.sequence_len):
            issues.append("dataset sequence_len does not match model shape")

        angles = np.asarray(ckpt.get("angles_deg", []), dtype=np.float32).reshape(-1)
        expected_angles = np.arange(0.0, 360.0, 5.0, dtype=np.float32)
        if angles.shape != expected_angles.shape or not np.allclose(angles, expected_angles, atol=1e-6):
            issues.append("angle labels must be 0..355 deg with 5 deg step")
        source_angles = np.asarray(source_meta.get("angle_labels_deg", []), dtype=np.float32).reshape(-1)
        if source_angles.shape != expected_angles.shape or not np.allclose(source_angles, expected_angles, atol=1e-6):
            issues.append("source metadata angle_labels_deg mismatch")

        pairs = np.asarray(source_meta.get("pair_list", []), dtype=np.int64)
        expected_pairs = np.asarray(
            [(i, (i + 1) % 16) for i in range(16)]
            + [(i, i + 8) for i in range(8)]
            + [(i, i + 4) for i in range(8)],
            dtype=np.int64,
        )
        if pairs.shape != expected_pairs.shape or not np.array_equal(pairs, expected_pairs):
            issues.append("pair_list does not match compact32_pairs")

        actual_quality = _metadata_dict(source_meta.get("quality_config", {}))
        expected_quality = PairQualityConfig().to_dict()
        for key, expected in expected_quality.items():
            if key not in actual_quality:
                issues.append(f"missing quality_config.{key}")
            elif abs(float(actual_quality[key]) - float(expected)) > 1e-8:
                issues.append(
                    f"quality_config.{key}: expected {expected}, got {actual_quality[key]}"
                )
        if list(source_meta.get("global_quality_order", [])) != list(GLOBAL_QUALITY_FIELDS):
            issues.append("global_quality_order does not match the formal six-feature contract")

        feature_dim = int(shape.sequence_len) * (
            int(shape.srp_dim) + int(shape.pair_reliability_dim) + int(shape.global_quality_dim)
        )
        mean = np.asarray(ckpt.get("mean", []), dtype=np.float32).reshape(-1)
        std = np.asarray(ckpt.get("std", []), dtype=np.float32).reshape(-1)
        if mean.size != feature_dim or std.size != feature_dim:
            issues.append(f"normalization size must be {feature_dim}")
        if not np.isfinite(mean).all() or not np.isfinite(std).all() or np.any(std <= 0.0):
            issues.append("normalization mean/std must be finite and std > 0")
        if float(ckpt.get("temperature", 0.0)) <= 0.0:
            issues.append("temperature must be > 0")

        if issues:
            raise ValueError("checkpoint contract mismatch: " + "; ".join(issues))

    def load(self) -> str:
        if self.loaded:
            return self.status
        if not self.ssl_nn_dir.exists():
            raise FileNotFoundError(f"ssl_nn directory not found: {self.ssl_nn_dir}")
        if not self.checkpoint_path.exists():
            raise FileNotFoundError(f"Proposed checkpoint not found: {self.checkpoint_path}")
        ssl_nn_text = str(self.ssl_nn_dir)
        while ssl_nn_text in sys.path:
            sys.path.remove(ssl_nn_text)
        sys.path.insert(0, ssl_nn_text)

        import torch
        from models.ca_qw_srp_cctcn import CaQwSrpCctcnShape, build_model
        from config import SSLConfig, angle_classes, compact32_pairs
        from gcc_phat_features import extract_gcc_phat_features
        from quality_weighted_srp_phat import build_ca_qw_features, load_calibration

        try:
            ckpt = torch.load(str(self.checkpoint_path), map_location="cpu", weights_only=False)
        except TypeError:
            ckpt = torch.load(str(self.checkpoint_path), map_location="cpu")
        model_name = str(ckpt.get("model_name", ""))
        if model_name != "ca_qw_srp_cctcn":
            raise ValueError(f"checkpoint model_name must be ca_qw_srp_cctcn, got {model_name!r}")

        shape_payload = dict(ckpt.get("shape", {}))
        shape = CaQwSrpCctcnShape(**shape_payload) if shape_payload else CaQwSrpCctcnShape()
        self._validate_checkpoint_contract(ckpt, shape)
        if int(shape.sequence_len) != self.sequence_len:
            self.sequence_len = int(shape.sequence_len)
            self._seq = deque(maxlen=self.sequence_len)
        model = build_model(shape)
        model.load_state_dict(ckpt["state_dict"])
        model.eval()

        cfg = SSLConfig(
            fs=OFFICIAL_SAMPLE_RATE,
            channels=OFFICIAL_CHANNELS,
            array_geometry="circle16",
            circle_radius_m=OFFICIAL_ARRAY_RADIUS_M,
            frame_len=OFFICIAL_FRAME_SAMPLES,
            max_lag=OFFICIAL_MAX_LAG,
            feature_method="gcc_phat",
        )
        pairs = compact32_pairs(cfg)
        angles = np.asarray(ckpt.get("angles_deg", angle_classes(cfg)), dtype=np.float32)
        if self.calibration_path is not None and not self.calibration_path.exists():
            raise FileNotFoundError(
                "formal CA-QW localization requires an existing calibration file: "
                f"{self.calibration_path}"
            )
        if self.calibration_path is None and not self.allow_identity_calibration:
            raise FileNotFoundError(
                "formal CA-QW localization requires measured calibration; "
                "identity calibration is allowed only for explicit simulation/self-test"
            )
        calibration = load_calibration(
            self.calibration_path,
            pair_count=pairs.shape[0],
            channel_count=cfg.channels,
        )

        self._torch = torch
        self._model = model
        self._shape = shape
        self._cfg = cfg
        self._pairs = pairs
        self._angles = angles
        self._mean = np.asarray(ckpt["mean"], dtype=np.float32)
        self._std = np.asarray(ckpt["std"], dtype=np.float32)
        self._temperature = float(ckpt.get("temperature", 1.0))
        self._calibration = calibration
        self._extract_gcc = extract_gcc_phat_features
        self._build_ca_qw = build_ca_qw_features
        self._meta = ckpt.get("meta", {})
        self.loaded = True
        self.status = self._format_status()
        return self.status

    def _format_status(self) -> str:
        cal_version = ""
        if isinstance(self._calibration, dict):
            cal_version = str(self._calibration.get("calibration_version", "default_identity_v1"))
        meta = self._meta if isinstance(self._meta, dict) else {}
        param_count = meta.get("parameter_count", "")
        macs = meta.get("macs_per_inference", "")
        calibration_mode = (
            "measured_file" if self.calibration_path is not None else "identity_simulation_only"
        )
        return (
            f"CA-QW-SRP-CCTCN ready: {self.checkpoint_path.name}; "
            f"seq={self.sequence_len}, angles={len(self._angles)}, "
            f"T={self._temperature:g}, params={param_count}, macs={macs}, "
            f"calibration={cal_version or 'unknown'} ({calibration_mode})"
        )

    def _sequence_input(self) -> tuple[np.ndarray, bool]:
        warmup = len(self._seq) < self.sequence_len
        if not self._seq:
            raise RuntimeError("sequence buffer is empty")
        items = list(self._seq)
        while len(items) < self.sequence_len:
            items.insert(0, items[0])
        srp = np.stack([item[0] for item in items], axis=0).astype(np.float32)
        rel = np.stack([item[1] for item in items], axis=0).astype(np.float32)
        gq = np.stack([item[2] for item in items], axis=0).astype(np.float32)
        x = np.concatenate([srp, rel, gq], axis=1).reshape(1, -1).astype(np.float32)
        return x, warmup

    def infer_frame(self, frame: np.ndarray, frame_id: int) -> ProposedLocalizationResult:
        self.load()
        pcm = np.asarray(frame, dtype=np.float32)
        if pcm.shape != (int(self._cfg.frame_len), int(self._cfg.channels)):
            raise ValueError(
                f"frame must be [{OFFICIAL_FRAME_SAMPLES},{OFFICIAL_CHANNELS}], got {pcm.shape}"
            )

        gcc = self._extract_gcc(
            pcm,
            pairs=self._pairs,
            cfg=self._cfg,
            fft_len=OFFICIAL_FFT_SIZE,
            phat_epsilon=OFFICIAL_PHAT_EPSILON,
            flatten=True,
        )
        feats = self._build_ca_qw(
            gcc,
            calibration=self._calibration,
            cfg=self._cfg,
            pairs=self._pairs,
            angles_deg=self._angles,
        )
        weighted_srp = np.asarray(feats["weighted_srp"], dtype=np.float32).reshape(-1)
        rel = np.asarray(feats["pair_reliability"], dtype=np.float32).reshape(-1)
        gq = np.asarray(feats["global_quality"], dtype=np.float32).reshape(-1)
        self._seq.append((weighted_srp, rel, gq))
        x_raw, warmup = self._sequence_input()
        x = ((x_raw - self._mean) / np.maximum(self._std, 1e-12)).astype(np.float32)

        torch = self._torch
        with torch.no_grad():
            logits_t, conf_head_t = self._model(torch.from_numpy(x))
            logits = logits_t.cpu().numpy()[0].astype(np.float32)
            conf_head = float(conf_head_t.cpu().numpy().reshape(-1)[0])

        z = logits / max(self._temperature, 1e-6)
        z = z - float(np.max(z))
        probs = np.exp(z)
        probs = probs / (float(np.sum(probs)) + 1e-12)
        order = np.argsort(-logits, kind="stable")
        top3 = order[:3].astype(int)
        angle_idx = int(top3[0])
        entropy = float(-np.sum(probs * np.log(probs + 1e-12)))
        sorted_srp = np.sort(weighted_srp)
        margin = float(sorted_srp[-1] - sorted_srp[-2]) if sorted_srp.size >= 2 else 0.0
        valid_pair_count = int(np.count_nonzero(rel > 0.1))
        confidence = float(probs[angle_idx])
        if warmup:
            invalid_reason = "sequence_warmup"
        elif valid_pair_count < self.min_valid_pairs:
            invalid_reason = "insufficient_valid_pairs"
        elif confidence < self.confidence_threshold:
            invalid_reason = "low_confidence"
        else:
            invalid_reason = ""

        return ProposedLocalizationResult(
            method="CA-QW-SRP-CCTCN",
            frame=int(frame_id),
            angle=float(self._angles[angle_idx] % 360.0),
            confidence=confidence,
            distance=None,
            top3_classes=[int(v) for v in top3.tolist()],
            top3_angles_deg=[float(self._angles[int(v)] % 360.0) for v in top3.tolist()],
            entropy=entropy,
            confidence_head_raw=conf_head,
            srp_peak_margin=margin,
            mean_pair_reliability=float(np.mean(rel)),
            valid_pair_count=valid_pair_count,
            warmup=bool(warmup),
            frame_valid=not invalid_reason,
            invalid_reason=invalid_reason,
        )


def metadata_preview(checkpoint_path: Path) -> str:
    """Return a short checkpoint preview without constructing the model."""
    path = Path(checkpoint_path)
    if not path.exists():
        return f"checkpoint missing: {path}"
    try:
        import torch

        try:
            ckpt = torch.load(str(path), map_location="cpu", weights_only=False)
        except TypeError:
            ckpt = torch.load(str(path), map_location="cpu")
        meta = ckpt.get("meta", {})
        shape = ckpt.get("shape", {})
        payload = {
            "model_name": ckpt.get("model_name", ""),
            "temperature": ckpt.get("temperature", ""),
            "shape": shape,
            "meta": {
                "feature_frontend": meta.get("feature_frontend", ""),
                "parameter_count": meta.get("parameter_count", ""),
                "macs_per_inference": meta.get("macs_per_inference", ""),
            },
        }
        return json.dumps(payload, ensure_ascii=False, indent=2)
    except Exception as exc:  # noqa: BLE001 - shown in GUI status panel
        return f"checkpoint preview failed: {exc}"


def circular_entropy_deg(probabilities: np.ndarray) -> float:
    p = np.asarray(probabilities, dtype=np.float32).reshape(-1)
    p = p / (float(np.sum(p)) + 1e-12)
    return float(-np.sum(p * np.log(p + 1e-12)))
