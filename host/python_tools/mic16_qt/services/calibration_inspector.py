"""只读检查现有标定文件，不提供隐式默认标定，也不上传参数。"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

import numpy as np


@dataclass(frozen=True)
class CalibrationInspection:
    path: Path
    sha256: str
    version: str
    issues: tuple[str, ...]
    warnings: tuple[str, ...]
    channel_delays: tuple[float, ...]
    channel_health: tuple[int, ...]
    pair_health: tuple[float, ...]
    azimuth_offset_deg: float | None

    @property
    def structurally_valid(self) -> bool:
        return not self.issues


def inspect_calibration(path: Path) -> CalibrationInspection:
    path = Path(path).resolve()
    # 采用有界读取，拒绝重复JSON键及NaN，不用看似合理的
    # 单位标定替代；本功能只检查文件，不认证真实性。
    with path.open("rb") as stream:
        raw = stream.read(1048577)
    if len(raw) > 1048576:
        raise ValueError("校准文件超过1MiB，拒绝读取")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"JSON重复字段：{key}")
            result[key] = value
        return result
    def bad_constant(value):
        raise ValueError(f"JSON非有限值：{value}")
    data = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=pairs, parse_constant=bad_constant)
    if not isinstance(data, dict):
        raise ValueError("校准JSON顶层必须是对象")
    issues, warnings = [], []
    def vector(name, size, low=None, high=None, binary=False):
        value = data.get(name)
        if not isinstance(value, list) or len(value) != size or any(type(v) not in (int, float) for v in value):
            issues.append(f"{name}需要{size}个数值，不能自动补默认值")
            return ()
        try:
            array = np.asarray(value, dtype=np.float64)
        except (TypeError, ValueError, OverflowError):
            issues.append(f"{name}数值无法表示为有限浮点数")
            return ()
        if not np.isfinite(array).all() or (low is not None and np.any(array < low)) or (high is not None and np.any(array > high)) or (binary and np.any((array != 0) & (array != 1))):
            issues.append(f"{name}范围/有限性不合法")
            return ()
        return tuple(int(v) if binary else float(v) for v in array)
    delays = vector("channel_delay_offsets_samples", 16)
    health = vector("channel_health_mask", 16, binary=True)
    pair_health = vector("pair_health_weights", 32, 0, 1)
    if "pair_enable_mask" in data:
        vector("pair_enable_mask", 32, binary=True)
    if data.get("pair_delay_offsets_source") == "explicit":
        vector("pair_delay_offsets_samples", 32)
    version = data.get("calibration_version")
    if not isinstance(version, str) or not version.strip():
        issues.append("缺少calibration_version")
        version = "unknown"
    offset = data.get("array_azimuth_offset_deg")
    try:
        offset = float(offset) if type(offset) in (int, float) else float("nan")
    except (ValueError, OverflowError):
        offset = float("nan")
    if not np.isfinite(offset):
        issues.append("array_azimuth_offset_deg需要有限数值")
        offset = None
    for name in ("calibration_dataset_hash", "channel_map_hash", "geometry_contract_hash"):
        if not isinstance(data.get(name), str) or not data[name].strip():
            warnings.append(f"缺少来源信息：{name}")
    if "identity" in version.lower():
        warnings.append("文件声明理想/单位校准，只能作为仿真/自测，不是实物标定")
    warnings.append("文件结构和哈希不证明实测来源；未应用至冻结二维算法或板端；RTOS目前无校准上传接口")
    return CalibrationInspection(path, hashlib.sha256(raw).hexdigest(), version,
        tuple(issues), tuple(warnings), delays, health, pair_health, offset)
