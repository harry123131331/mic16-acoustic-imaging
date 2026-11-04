# -*- coding: utf-8 -*-
"""One reproducible path from a MIC16 recording to a MATLAB R2024a report."""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

import numpy as np

from mic16_config import OFFICIAL_ARRAY_RADIUS_M, OFFICIAL_CHANNELS, OFFICIAL_SAMPLE_RATE
from mic16_project_paths import (
    find_matlab_executable,
    matlab_capture_reports_root,
    matlab_tools_dir,
)


DATASET_ROLES = (
    "calibration",
    "real_train",
    "real_val",
    "real_test",
    "unlabeled_diagnostic",
    "noise_reference",
)
SIGNAL_TYPES = ("tone", "chirp", "mls", "speech", "white_noise", "pink_noise", "silence", "other")
GROUND_TRUTH_ROLES = {"calibration", "real_train", "real_val", "real_test"}
TRAINING_ROLES = {"real_train"}
AZEL_GEOMETRY_CONTRACT_VERSION = "mic16_planar_front_azel_geometry_v1"
INT32_MIN = np.iinfo(np.int32).min
INT32_MAX = np.iinfo(np.int32).max
MAX_RAIL_RATIO = 1.0e-4


@dataclass(frozen=True)
class CaptureLabels:
    dataset_role: str
    signal_type: str
    session_id: str
    scene_id: str
    source_id: str = ""
    azimuth_deg: float | None = None
    elevation_deg: float | None = None
    distance_m: float | None = None
    tone_hz: float = 0.0
    notes: str = ""
    azel_geometry_contract: str = ""
    array_pose_id: str = ""
    angle_measurement_method: str = ""

    @property
    def has_ground_truth(self) -> bool:
        return self.dataset_role in GROUND_TRUTH_ROLES and self.azimuth_deg is not None

    @property
    def calibration_only(self) -> bool:
        return self.dataset_role == "calibration"

    @property
    def has_azel_ground_truth(self) -> bool:
        return self.has_ground_truth and self.elevation_deg is not None and math.isfinite(self.elevation_deg)

    def validate(self) -> list[str]:
        issues: list[str] = []
        if self.dataset_role not in DATASET_ROLES:
            issues.append(f"unsupported dataset_role: {self.dataset_role}")
        if self.signal_type not in SIGNAL_TYPES:
            issues.append(f"unsupported signal_type: {self.signal_type}")
        if not self.session_id.strip():
            issues.append("session_id is required")
        if not self.scene_id.strip():
            issues.append("scene_id is required")
        if self.dataset_role in GROUND_TRUTH_ROLES:
            if self.azimuth_deg is None or not math.isfinite(self.azimuth_deg):
                issues.append("azimuth_deg is required for labeled recordings")
            if self.distance_m is None or not math.isfinite(self.distance_m) or self.distance_m <= 0:
                issues.append("distance_m must be positive for labeled recordings")
        if self.elevation_deg is not None:
            if not math.isfinite(self.elevation_deg) or not -90.0 <= self.elevation_deg <= 90.0:
                issues.append("elevation_deg must be finite and within [-90, 90]")
            if not self.array_pose_id.strip():
                issues.append("array_pose_id is required when elevation_deg is provided")
            if not self.angle_measurement_method.strip():
                issues.append("angle_measurement_method is required when elevation_deg is provided")
            if self.azel_geometry_contract != AZEL_GEOMETRY_CONTRACT_VERSION:
                issues.append(
                    "azel_geometry_contract must equal "
                    f"{AZEL_GEOMETRY_CONTRACT_VERSION} when elevation_deg is provided"
                )
        if self.signal_type == "tone" and self.tone_hz <= 0:
            issues.append("tone_hz must be positive for a tone recording")
        if self.dataset_role == "noise_reference" and self.signal_type != "silence":
            issues.append("noise_reference must use signal_type=silence")
        return issues


def safe_component(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value.strip())
    return cleaned.strip("_") or "unnamed"


def build_record_id(labels: CaptureLabels, timestamp: str | None = None) -> str:
    stamp = timestamp or datetime.now().strftime("%Y%m%d_%H%M%S")
    if labels.azimuth_deg is None:
        angle = "no_angle"
    else:
        angle = f"{float(labels.azimuth_deg) % 360.0:05.1f}deg".replace(".", "p")
    elevation = ""
    if labels.elevation_deg is not None:
        sign = "p" if float(labels.elevation_deg) >= 0 else "m"
        magnitude = f"{abs(float(labels.elevation_deg)):04.1f}".replace(".", "p")
        elevation = f"_el{sign}{magnitude}deg"
    return safe_component(f"{labels.dataset_role}_{angle}{elevation}_{labels.signal_type}_{stamp}")


def audit_capture(raw_path: Path, metadata_path: Path | None = None, channels: int = OFFICIAL_CHANNELS) -> dict[str, object]:
    raw_path = Path(raw_path).resolve()
    raw = np.load(raw_path, mmap_mode="r")
    if raw.dtype != np.int32 or raw.ndim != 2 or raw.shape[1] != channels:
        return {
            "capture_valid": False,
            "invalid_reasons": [f"expected int32 [samples,{channels}], got {raw.dtype} {raw.shape}"],
            "raw_path": str(raw_path),
        }

    rail_by_channel = np.mean((raw == INT32_MIN) | (raw == INT32_MAX), axis=0)
    rail_ratio = float(np.mean(rail_by_channel))
    stride = max(1, raw.shape[0] // 65536)
    sampled = np.asarray(raw[::stride]).reshape(-1)
    unique_sampled = int(np.unique(sampled).size)
    dynamic_range = (np.max(raw, axis=0).astype(np.int64) - np.min(raw, axis=0).astype(np.int64))

    metadata: dict[str, object] = {}
    if metadata_path and Path(metadata_path).is_file():
        metadata = json.loads(Path(metadata_path).read_text(encoding="utf-8"))
    lost = int(metadata.get("lost_frames_after_start", 0) or 0)
    duplicate = int(metadata.get("duplicate_packets", 0) or 0)
    out_of_order = int(metadata.get("out_of_order_packets", 0) or 0)
    bad_packets = int(metadata.get("bad_packets", 0) or 0)

    reasons: list[str] = []
    if rail_ratio > MAX_RAIL_RATIO:
        reasons.append("int32_rail_saturation")
    if unique_sampled < 8:
        reasons.append("insufficient_numeric_diversity")
    if np.any(dynamic_range == 0):
        reasons.append("constant_channel")
    if lost or duplicate or out_of_order or bad_packets:
        reasons.append("udp_discontinuity_or_bad_packet")
    return {
        "audit_version": "mic16_capture_gate_v1",
        "raw_path": str(raw_path),
        "shape": [int(v) for v in raw.shape],
        "dtype": str(raw.dtype),
        "rail_sample_ratio": rail_ratio,
        "rail_ratio_by_channel": rail_by_channel.astype(float).tolist(),
        "sampled_unique_value_count": unique_sampled,
        "dynamic_range_by_channel": dynamic_range.astype(int).tolist(),
        "lost_packets": lost,
        "duplicate_packets": duplicate,
        "out_of_order_packets": out_of_order,
        "bad_packets": bad_packets,
        "capture_valid": not reasons,
        "invalid_reasons": reasons,
    }


def merge_capture_metadata(metadata_path: Path, labels: CaptureLabels, audit: dict[str, object]) -> dict[str, object]:
    metadata_path = Path(metadata_path)
    payload: dict[str, object] = {}
    if metadata_path.is_file():
        payload = json.loads(metadata_path.read_text(encoding="utf-8"))
    payload["capture_labels"] = {
        **asdict(labels),
        "has_ground_truth": labels.has_ground_truth,
        "calibration_only": labels.calibration_only,
        "valid_for_training_role": labels.dataset_role in TRAINING_ROLES,
    }
    payload["capture_audit"] = audit
    payload["capture_valid"] = bool(audit.get("capture_valid", False))
    payload["invalid_reasons"] = list(audit.get("invalid_reasons", []))
    metadata_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    return payload


def _matlab_quote(path: Path) -> str:
    return str(path).replace("'", "''")


def generate_matlab_report(
    raw_path: Path,
    metadata_path: Path,
    labels: CaptureLabels,
    noise_path: Path | None = None,
    segments_path: Path | None = None,
    report_id: str = "",
    report_root: Path | None = None,
    matlab_exe: str = "",
    render: bool = True,
) -> dict[str, object]:
    raw_path = Path(raw_path).resolve()
    metadata_path = Path(metadata_path).resolve()
    audit = audit_capture(raw_path, metadata_path)
    merge_capture_metadata(metadata_path, labels, audit)

    report_id = safe_component(report_id or raw_path.parent.name)
    root = Path(report_root).resolve() if report_root else matlab_capture_reports_root(__file__).resolve()
    report_dir = root / report_id
    mat_path = report_dir / "data" / f"{report_id}.mat"
    mat_path.parent.mkdir(parents=True, exist_ok=True)

    exporter = Path(__file__).resolve().parent / "mic16_export_matlab.py"
    command = [
        sys.executable,
        str(exporter),
        "--input", str(raw_path),
        "--metadata", str(metadata_path),
        "--out", str(mat_path),
        "--capture-name", report_id,
        "--signal-type", labels.signal_type,
        "--sample-rate", str(OFFICIAL_SAMPLE_RATE),
        "--channels", str(OFFICIAL_CHANNELS),
        "--radius-mm", str(OFFICIAL_ARRAY_RADIUS_M * 1000.0),
        "--tone-hz", str(labels.tone_hz),
        "--description", labels.notes or f"{labels.dataset_role}; {labels.scene_id}",
    ]
    if noise_path and Path(noise_path).is_file():
        command += ["--noise", str(Path(noise_path).resolve())]
    if segments_path and Path(segments_path).is_file():
        command += ["--segments", str(Path(segments_path).resolve())]
    errors: list[str] = []
    mat_exported = False
    matlab_rendered = False
    try:
        subprocess.run(command, check=True, cwd=str(exporter.parent))
        mat_exported = mat_path.is_file()
        if not mat_exported:
            errors.append("MAT exporter returned success but the MAT file is missing")
    except (OSError, subprocess.CalledProcessError) as exc:
        errors.append(f"MAT export failed: {exc}")

    if render and mat_exported:
        try:
            matlab = find_matlab_executable(matlab_exe)
            tools = matlab_tools_dir(__file__)
            batch = (
                f"addpath('{_matlab_quote(tools)}'); "
                f"plot_mic16_capture_report('{_matlab_quote(mat_path)}','{_matlab_quote(report_dir)}');"
            )
            subprocess.run([str(matlab), "-batch", batch], check=True, cwd=str(tools))
            matlab_rendered = True
        except (OSError, subprocess.CalledProcessError) as exc:
            errors.append(f"MATLAB render failed: {exc}")
    elif render and not mat_exported:
        errors.append("MATLAB render skipped because MAT export did not complete")

    result = {
        "pipeline_version": "mic16_capture_matlab_pipeline_v1",
        "report_id": report_id,
        "report_dir": str(report_dir),
        "mat_path": str(mat_path),
        "mat_exported": mat_exported,
        "matlab_render_requested": bool(render),
        "matlab_rendered": matlab_rendered,
        "pipeline_ok": bool(mat_exported and (matlab_rendered or not render)),
        "errors": errors,
        "labels": asdict(labels),
        "capture_audit": audit,
    }
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "pipeline_result.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(f"CAPTURE_VALID={str(bool(audit.get('capture_valid'))).lower()}")
    print(f"MAT_EXPORTED={str(mat_exported).lower()}")
    print(f"MATLAB_RENDERED={str(matlab_rendered).lower()}")
    if errors:
        print("PIPELINE_ERRORS=" + json.dumps(errors, ensure_ascii=False))
    print(f"MATLAB_REPORT={report_dir}")
    return result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Audit one MIC16 capture and generate its MATLAB report.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--metadata", required=True)
    parser.add_argument("--noise", default="")
    parser.add_argument("--segments", default="")
    parser.add_argument("--report-id", default="")
    parser.add_argument("--report-root", default="")
    parser.add_argument("--matlab-exe", default="")
    parser.add_argument("--skip-matlab", action="store_true")
    parser.add_argument("--dataset-role", choices=DATASET_ROLES, required=True)
    parser.add_argument("--signal-type", choices=SIGNAL_TYPES, required=True)
    parser.add_argument("--session-id", required=True)
    parser.add_argument("--scene-id", required=True)
    parser.add_argument("--source-id", default="")
    parser.add_argument("--azimuth-deg", type=float)
    parser.add_argument("--elevation-deg", type=float)
    parser.add_argument("--distance-m", type=float)
    parser.add_argument("--tone-hz", type=float, default=0.0)
    parser.add_argument("--notes", default="")
    parser.add_argument("--azel-geometry-contract", default="")
    parser.add_argument("--array-pose-id", default="")
    parser.add_argument("--angle-measurement-method", default="")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    labels = CaptureLabels(
        dataset_role=args.dataset_role,
        signal_type=args.signal_type,
        session_id=args.session_id,
        scene_id=args.scene_id,
        source_id=args.source_id,
        azimuth_deg=args.azimuth_deg,
        elevation_deg=args.elevation_deg,
        distance_m=args.distance_m,
        tone_hz=args.tone_hz,
        notes=args.notes,
        azel_geometry_contract=args.azel_geometry_contract,
        array_pose_id=args.array_pose_id,
        angle_measurement_method=args.angle_measurement_method,
    )
    issues = labels.validate()
    if issues:
        raise SystemExit("; ".join(issues))
    generate_matlab_report(
        Path(args.input),
        Path(args.metadata),
        labels,
        Path(args.noise) if args.noise else None,
        Path(args.segments) if args.segments else None,
        args.report_id,
        Path(args.report_root) if args.report_root else None,
        args.matlab_exe,
        not args.skip_matlab,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
