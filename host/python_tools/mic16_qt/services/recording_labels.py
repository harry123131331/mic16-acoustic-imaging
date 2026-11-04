"""原录制标签与来源证据、算法输入分开保存。"""
from __future__ import annotations

from dataclasses import asdict, dataclass
import math

from mic16_capture_pipeline import CaptureLabels


@dataclass(frozen=True)
class RecordingLabels:
    labels: CaptureLabels
    source_type: str = "loudspeaker"

    def validate(self) -> None:
        issues = self.labels.validate()
        if self.source_type not in ("loudspeaker", "human", "noise_source", "unknown"):
            issues.append("声源类型无效")
        for field in ("azimuth_deg", "elevation_deg", "distance_m", "tone_hz"):
            value = getattr(self.labels, field)
            if value is not None and (isinstance(value, bool) or not math.isfinite(value)):
                issues.append(f"{field} 必须为有限数字")
        for field, limit in (("session_id", 128), ("scene_id", 128), ("source_id", 128),
                             ("array_pose_id", 128), ("angle_measurement_method", 256), ("notes", 2048)):
            value = getattr(self.labels, field)
            if not isinstance(value, str) or len(value) > limit or "\x00" in value:
                issues.append(f"{field} 长度或内容无效")
        if issues:
            raise ValueError("录音标签未通过：\n" + "\n".join(issues))

    def metadata(self) -> dict:
        self.validate()
        return {
            **asdict(self.labels), "source_type": self.source_type,
            "has_ground_truth": self.labels.has_ground_truth,
            "has_azel_ground_truth": self.labels.has_azel_ground_truth,
            "calibration_only": self.labels.calibration_only,
            "valid_for_training_role": self.labels.dataset_role == "real_train",
            "label_origin": "USER_DECLARED_NOT_MEASUREMENT_VERIFIED",
            "claim_boundary_zh": "人工标签不认证实板、真实录音或角度真值；用途选择不自动纳入训练/验证/测试。",
        }
