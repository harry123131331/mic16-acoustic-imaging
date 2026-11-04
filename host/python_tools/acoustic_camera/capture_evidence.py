"""Evidence export for Phase-1 acoustic-camera screenshots."""

from __future__ import annotations

import hashlib
import json
import re
import time
from pathlib import Path
from typing import Mapping

import numpy as np


def _safe_prefix(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value.strip())
    return cleaned.strip("._") or "mic16_acoustic_camera"


def image_sha256(rgb: np.ndarray) -> str:
    array = np.ascontiguousarray(np.asarray(rgb, dtype=np.uint8))
    return hashlib.sha256(array.tobytes()).hexdigest()


def save_evidence_bundle(
    output_dir: Path,
    rgb: np.ndarray,
    metadata: Mapping[str, object],
    *,
    prefix: str = "mic16_acoustic_camera_phase1",
) -> tuple[Path, Path]:
    """Save a PNG and adjacent JSON with an integrity hash."""
    try:
        from PIL import Image
    except Exception as exc:  # noqa: BLE001
        raise RuntimeError("Pillow 未安装，无法导出 PNG 证据") from exc

    array = np.asarray(rgb, dtype=np.uint8)
    if array.ndim != 3 or array.shape[2] != 3:
        raise ValueError(f"expected RGB [H,W,3], got {array.shape}")
    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    stem = f"{_safe_prefix(prefix)}_{stamp}"
    png_path = out_dir / f"{stem}.png"
    json_path = out_dir / f"{stem}.json"
    Image.fromarray(array, mode="RGB").save(png_path)

    payload = dict(metadata)
    payload.update(
        {
            "contract_version": "mic16_acoustic_camera_phase1_evidence_v1",
            "evidence_level": "E1_GUI_VISUALIZATION",
            "saved_at_local": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "image_file": png_path.name,
            "image_width": int(array.shape[1]),
            "image_height": int(array.shape[0]),
            "image_rgb_sha256": image_sha256(array),
            "claim_boundary": (
                "One-dimensional azimuth overlay only. This artifact is not "
                "2-D acoustic imaging, elevation estimation, ILA, or board proof."
            ),
        }
    )
    json_path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True),
        encoding="utf-8",
    )
    return png_path, json_path

