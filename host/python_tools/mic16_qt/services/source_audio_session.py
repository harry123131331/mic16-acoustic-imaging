"""读取已保存逐源会话，不导入或执行检测器。WAV预览采用有界读取并验证元数据和声明的样本时间轴；加载不等于完成密码学或声学审计。"""
from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import struct

import numpy as np


def _inside(root: Path, name: str) -> Path:
    relative = Path(name)
    target = (root / relative).resolve()
    if relative.is_absolute() or not target.is_relative_to(root) or target == root:
        raise ValueError('会话文件路径超出所选目录')
    return target


def _json(path: Path):
    if path.stat().st_size > 65536:
        raise ValueError('会话头过大')
    return json.loads(path.read_text(encoding='utf-8-sig'))


def _rows(path: Path, limit: int):
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('元数据超过离线查看器的16MiB限制')
    with path.open(encoding='utf-8-sig') as stream:
        for index, line in enumerate(stream):
            if index >= limit or len(line) > 65536:
                raise ValueError('元数据记录过多或单行过长')
            yield json.loads(line)


@dataclass(frozen=True)
class AudioAsset:
    path: Path
    source_id: int | None
    kind: str
    frames: int
    sample_rate: int
    data_offset: int

    @classmethod
    def from_manifest(cls, root: Path, row: dict, total: int):
        path = _inside(root, row['path'])
        sid, kind = row['source_id'], row['kind']
        if ((sid is None and kind != 'input_MIC0') or
                (sid is not None and (type(sid) is not int or sid < 1 or kind not in ('WDAS', 'enhanced')))):
            raise ValueError('源ID或音频种类不合法')
        size = path.stat().st_size
        with path.open('rb') as stream:
            header = stream.read(12)
            if len(header) != 12 or header[:4] != b'RIFF' or header[8:] != b'WAVE':
                raise ValueError('仅支持本工程导出的RIFF WAV')
            end = struct.unpack('<I', header[4:8])[0] + 8
            if end != size:
                raise ValueError('WAV长度与RIFF头不符')
            fmt = data = None
            for _ in range(4096):
                if stream.tell() == end:
                    break
                part = stream.read(8)
                if len(part) != 8:
                    raise ValueError('WAV块头不完整')
                name, length = struct.unpack('<4sI', part)
                start = stream.tell()
                if start + length + (length % 2) > end:
                    raise ValueError('WAV数据被截断')
                if name == b'fmt ':
                    if fmt is not None or length < 16:
                        raise ValueError('WAV格式块重复或不完整')
                    fmt = struct.unpack('<HHIIHH', stream.read(16))
                elif name == b'data':
                    if data is not None:
                        raise ValueError('WAV存在重复数据块')
                    data = (start, length)
                stream.seek(start + length + length % 2)
            else:
                raise ValueError('WAV块数量过多')
        if fmt != (3, 1, 16000, 64000, 4, 32) or data is None or data[1] % 4:
            raise ValueError('需要16kHz、单通道、Float32逐源WAV')
        frames = data[1] // 4
        if frames != total or row['frames'] != frames or row['sample_rate'] != 16000:
            raise ValueError('音频帧数与会话时间轴不符')
        return cls(path, sid, kind, frames, 16000, data[0])

    def preview(self, start_sample: int = 0, count: int = 16000):
        start = max(0, min(self.frames, int(start_sample)))
        count = max(0, min(32000, self.frames - start, int(count)))
        with self.path.open('rb') as stream:
            stream.seek(self.data_offset + 4 * start)
            values = np.frombuffer(stream.read(count * 4), dtype='<f4').copy()
        if len(values) != count or not np.isfinite(values).all():
            raise ValueError('预览音频被截断或包含无效数值')
        return np.arange(start, start + count) / self.sample_rate, values


@dataclass(frozen=True)
class SourceAudioSession:
    directory: Path
    segment_id: str
    pose_key: str
    evidence_source: str
    output_samples: int
    assets: dict
    first_confirmation: dict

    @property
    def source_ids(self):
        return sorted({sid for sid, kind in self.assets if sid is not None})

    @classmethod
    def load(cls, directory):
        root = Path(directory).resolve(strict=True)
        info = _json(root / 'session.json')
        total = info['output_samples']
        network = info['schema_version'] == 2
        if (info['schema_version'] not in (1, 2) or info['status'] != 'COMPLETED' or
                info['evidence_source'] not in (('UDP_RECEIVED_UNVERIFIED', 'LOCAL_UDP_TEST') if network else ('SIMULATED_REPLAY', 'FILE_REPLAY')) or
                info['audio_rate'] != 16000 or type(total) is not int or total < 1):
            raise ValueError('请选择已完整保存的逐源会话；未完成的UDP段不能作为完整录音打开')
        if network and (info.get('input_MIC0_present') is not False or info.get('missing_transport_audio_filled') is not False):
            raise ValueError('逐源网络会话不能伪造MIC0或补齐丢包')
        assets = {}
        for row in _rows(_inside(root, info['audio_manifest']), 4096):
            asset = AudioAsset.from_manifest(root, row, total)
            key = (asset.source_id, asset.kind)
            if key in assets:
                raise ValueError('音频清单包含重复源/种类')
            assets[key] = asset
        ids = sorted({sid for sid, _ in assets if sid is not None})
        if (len(assets) != info['audio_files'] or
                (not network and (None, 'input_MIC0') not in assets) or
                (network and (None, 'input_MIC0') in assets)):
            raise ValueError('音频清单不完整')
        for sid in ids:
            if any((sid, kind) not in assets for kind in ('WDAS', 'enhanced')):
                raise ValueError('逐源WDAS/增强文件缺失')
        first, previous, packets = {}, 0, 0
        for row in _rows(_inside(root, info['activity_metadata']), 100000):
            begin, end = row['output_start_sample'], row['output_end_sample_exclusive']
            if (row['segment_id'] != info['segment_id'] or row['pose_key'] != info['pose_key'] or
                    row['output_rate'] != 16000 or begin != previous or
                    type(end) is not int or end <= begin or end > total):
                raise ValueError('逐源会话记录存在缺口、重复或姿态变更')
            for source in row['current_confirmed']:
                sid = source['source_id']
                if sid not in ids:
                    raise ValueError('确认记录引用未保存的源')
                first.setdefault(sid, begin)
            previous = end
            packets += 1
        if previous != total or packets != info['packets']:
            raise ValueError('会话时间轴未完整保存')
        return cls(root, str(info['segment_id']), str(info['pose_key']),
                   info['evidence_source'], total, assets, first)
