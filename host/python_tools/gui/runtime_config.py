# -*- coding: utf-8 -*-
"""GUI runtime configuration for the formal MIC16 software chain.

The JSON file may store project-relative paths so a packaged project can be
moved to another machine without retaining a developer-specific absolute path.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from mic16_config import (
    OFFICIAL_ANGLE_STEP_DEG,
    OFFICIAL_ARRAY_RADIUS_M,
    OFFICIAL_CHANNELS,
    OFFICIAL_FFT_SIZE,
    OFFICIAL_FRAME_SAMPLES,
    OFFICIAL_MAX_LAG,
    OFFICIAL_SAMPLE_RATE,
)
from mic16_project_paths import workspace_root


def _find_repo_root(start: Path) -> Path:
    current = start.resolve()
    for parent in (current, *current.parents):
        if (parent / "ssl_nn").exists() and (parent / "mic16_next_version").exists():
            return parent
    shared_root = workspace_root(start)
    if (shared_root / "ssl_nn").exists() and (shared_root / "mic16_next_version").exists():
        return shared_root
    # 正式包布局使本文件位于根目录下三级。
    module_path = Path(__file__).resolve()
    return module_path.parents[3]


def default_checkpoint_path(start: Path) -> Path:
    root = _find_repo_root(start)
    return (
        root
        / "ssl_nn"
        / "outputs"
        / "formal_mvdr_calibration_consistent_multi_array_v1"
        / "ca_qw_srp_cctcn"
        / "checkpoints"
        / "best.pt"
    )


def default_calibration_path(start: Path) -> Path:
    root = _find_repo_root(start)
    return root / "mic16_next_version" / "calibration" / "mic16_calibration.json"


def _resolve_project_path(value: str, root: Path) -> str:
    if not value:
        return ""
    path = Path(value).expanduser()
    return str(path if path.is_absolute() else root / path)


def _portable_project_path(value: str, root: Path) -> str:
    if not value:
        return ""
    path = Path(value).expanduser()
    if not path.is_absolute():
        return path.as_posix()
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


@dataclass
class GuiRuntimeConfig:
    config_version: str = "mic16_gui_runtime_v4"
    formal_algorithm: str = "CA-QW-SRP-CCTCN"
    sample_rate_hz: float = OFFICIAL_SAMPLE_RATE
    channels: int = OFFICIAL_CHANNELS
    frame_samples: int = OFFICIAL_FRAME_SAMPLES
    array_radius_m: float = OFFICIAL_ARRAY_RADIUS_M
    gcc_fft_len: int = OFFICIAL_FFT_SIZE
    gcc_max_lag: int = OFFICIAL_MAX_LAG
    angle_step_deg: int = OFFICIAL_ANGLE_STEP_DEG
    sequence_len: int = 5
    confidence_threshold: float = 0.7880566716194153
    min_valid_pairs: int = 20
    default_backend: str = "CA-QW-SRP-CCTCN"
    fallback_backend: str = "SRP-PHAT"
    checkpoint_path: str = ""
    calibration_path: str = ""
    allow_identity_calibration: bool = False
    replay_root: str = ""
    notes: str = "Python GUI runtime config only; not HLS or board verification."
    extra: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def defaults(cls, tool_dir: Path) -> "GuiRuntimeConfig":
        root = _find_repo_root(tool_dir)
        return cls(
            checkpoint_path=str(default_checkpoint_path(tool_dir)),
            calibration_path=str(default_calibration_path(tool_dir)),
            replay_root=str(root / "mic16_next_version" / "results"),
        )

    def validate_formal_constants(self) -> list[str]:
        issues: list[str] = []
        if abs(float(self.sample_rate_hz) - OFFICIAL_SAMPLE_RATE) > 1e-6:
            issues.append(f"sample_rate_hz expected {OFFICIAL_SAMPLE_RATE}, got {self.sample_rate_hz}")
        if int(self.channels) != OFFICIAL_CHANNELS:
            issues.append(f"channels expected {OFFICIAL_CHANNELS}, got {self.channels}")
        if int(self.frame_samples) != OFFICIAL_FRAME_SAMPLES:
            issues.append(f"frame_samples expected {OFFICIAL_FRAME_SAMPLES}, got {self.frame_samples}")
        if abs(float(self.array_radius_m) - OFFICIAL_ARRAY_RADIUS_M) > 1e-9:
            issues.append(f"array_radius_m expected {OFFICIAL_ARRAY_RADIUS_M}, got {self.array_radius_m}")
        if int(self.gcc_fft_len) != OFFICIAL_FFT_SIZE:
            issues.append(f"gcc_fft_len expected {OFFICIAL_FFT_SIZE}, got {self.gcc_fft_len}")
        if int(self.gcc_max_lag) != OFFICIAL_MAX_LAG:
            issues.append(f"gcc_max_lag expected {OFFICIAL_MAX_LAG}, got {self.gcc_max_lag}")
        if int(self.angle_step_deg) != OFFICIAL_ANGLE_STEP_DEG:
            issues.append(f"angle_step_deg expected {OFFICIAL_ANGLE_STEP_DEG}, got {self.angle_step_deg}")
        if int(self.sequence_len) != 5:
            issues.append(f"sequence_len expected 5, got {self.sequence_len}")
        if not 0.0 <= float(self.confidence_threshold) <= 1.0:
            issues.append(f"confidence_threshold must be in [0,1], got {self.confidence_threshold}")
        if not 1 <= int(self.min_valid_pairs) <= 32:
            issues.append(f"min_valid_pairs must be in [1,32], got {self.min_valid_pairs}")
        return issues


def load_runtime_config(path: Path, tool_dir: Path) -> GuiRuntimeConfig:
    if not path.exists():
        return GuiRuntimeConfig.defaults(tool_dir)
    data = json.loads(path.read_text(encoding="utf-8"))
    cfg = GuiRuntimeConfig.defaults(tool_dir)
    for key, value in data.items():
        if hasattr(cfg, key):
            setattr(cfg, key, value)
        else:
            cfg.extra[key] = value
    root = _find_repo_root(tool_dir)
    cfg.checkpoint_path = _resolve_project_path(cfg.checkpoint_path, root)
    cfg.calibration_path = _resolve_project_path(cfg.calibration_path, root)
    cfg.replay_root = _resolve_project_path(cfg.replay_root, root)
    return cfg


def save_runtime_config(path: Path, cfg: GuiRuntimeConfig) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = asdict(cfg)
    root = _find_repo_root(path.parent)
    for key in ("checkpoint_path", "calibration_path", "replay_root"):
        payload[key] = _portable_project_path(str(payload[key]), root)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
