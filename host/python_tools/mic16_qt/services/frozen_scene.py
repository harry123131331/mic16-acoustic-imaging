"""只读冻结场景目录，不生成场景、不训练、不调参。"""
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys

from mic16_qt.services.experiment_contract import TOOL_ROOT

BOUNDARY = "既有合成HOLD场景的确定性复现，不新增性能实验、不改变冻结阈值；不代表实物/FPGA结果。"


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path):
    if path.stat().st_size > 1024 * 1024:
        raise ValueError("冻结JSON超过1MiB")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("冻结JSON重复字段：" + key)
            result[key] = value
        return result
    def invalid(value):
        raise ValueError("冻结JSON非有限值：" + value)
    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=pairs, parse_constant=invalid)


@dataclass(frozen=True)
class FrozenLocations:
    host: Path
    evidence: Path
    adapter_root: Path


def locations(host=None):
    host = Path(host or TOOL_ROOT).resolve()
    package = host.parent.parent
    if (package / "MANIFEST.sha256").is_file():
        return FrozenLocations(host, package / "data/frozen_multisource_hold", package / "data/frozen_model")
    return FrozenLocations(host, host.parent / "results/tf_zone_semantic_fresh_hold_v1", host.parent)


def catalog(where=None):
    where = where or locations()
    manifest = where.evidence / "scene_manifest.json"
    summary = read_json(where.evidence / "summary.json")
    if summary.get("status") != "PASS_FRESH_HOLD" or summary.get("passed") is not True:
        raise ValueError("冻结HOLD状态不合法")
    if sha(manifest) != summary.get("scene_manifest_sha256"):
        raise ValueError("冻结场景清单哈希不符")
    rows = read_json(manifest)
    if not isinstance(rows, list) or len(rows) != 33:
        raise ValueError("需要原冻结33场景清单")
    seen = set()
    for row in rows:
        sid = row.get("scene_id", "")
        if not isinstance(sid, str) or not re.fullmatch(r"hold_[a-z0-9_]{1,100}", sid) or sid in seen:
            raise ValueError("重复/非法冻结场景ID")
        if row.get("room") not in ("compact_room", "classroom", "hall"):
            raise ValueError("非法冻结房间")
        seen.add(sid)
    return rows, sha(manifest)


@dataclass(frozen=True)
class FrozenSceneRequest:
    scene_id: str = "hold_compact_room_diffuse_noise_only"
    manifest_sha256: str = ""
    python: str = sys.executable

    def canonical(self):
        rows, digest = catalog()
        if self.scene_id not in {row["scene_id"] for row in rows}:
            raise ValueError("只能复现已冻结场景，不能生成新HOLD场景")
        if self.manifest_sha256 != digest:
            raise ValueError("界面场景清单已变化，请重新打开场景页")
        if not Path(self.python).is_file():
            raise ValueError("请选择已有NumPy/SciPy/Matplotlib科研Python")
        return FrozenSceneRequest(self.scene_id, digest, str(Path(self.python).resolve()))
