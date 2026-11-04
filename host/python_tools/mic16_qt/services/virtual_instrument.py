"""有界、只读的电脑观测器，不是FPGA ILA，也不是信号源。"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import csv
import hashlib
import json
from pathlib import Path
import time
import uuid

import numpy as np

from mic16_qt.core.models import AcousticFrame

EVENT_KINDS = ("AUDIO", "STATUS", "RESULT", "ENHANCED", "ERROR")
SOURCES = {"BOARD LIVE", "PC LIVE", "REPLAY", "SYNTHETIC"}


@dataclass(frozen=True)
class InstrumentEvent:
    receive_monotonic_s: float
    kind: str
    frame_id: int | None
    detail: str


@dataclass(frozen=True)
class InstrumentSnapshot:
    samples: np.ndarray
    sample_rate_hz: float
    units: str
    source: str
    session_id: str
    reference_monotonic_s: float
    events: tuple[InstrumentEvent, ...]
    state: str
    event_evictions: int
    continuity_resets: int

    def channel_window(self, channel: int, timebase_ms: int) -> tuple[np.ndarray, np.ndarray]:
        if channel not in range(16) or timebase_ms not in (50, 100, 200, 500, 1000):
            raise ValueError("通道或时间基准不合法")
        n = min(self.samples.shape[0], int(self.sample_rate_hz * timebase_ms / 1000))
        pcm = self.samples[-n:, channel].copy() if n else np.empty(0, dtype=self.samples.dtype)
        axis = (np.arange(n, dtype=np.float64) - max(0, n - 1)) * 1000 / self.sample_rate_hz
        return axis, pcm


class VirtualInstrumentObserver:
    """每会话最多保留1秒PCM及4096条电脑分派事件。保持/触发保存独立不可变副本，采集继续；PCM时间相对最后接收样本，事件时间为电脑分派时刻，不是硬件时间戳或样本同步叠加。"""
    def __init__(self, event_capacity: int = 4096) -> None:
        if not 1 <= event_capacity <= 4096:
            raise ValueError("事件容量必须在1至4096之间")
        self.event_capacity = event_capacity
        self.reset("SYNTHETIC")

    def reset(self, source: str) -> None:
        if source not in SOURCES:
            raise ValueError("未知数据来源")
        self.source = source
        self.session_id = uuid.uuid4().hex
        self.events: deque[InstrumentEvent] = deque(maxlen=self.event_capacity)
        self.chunks: deque[np.ndarray] = deque()
        self.sample_count = 0
        self.sample_rate_hz = 39062.5
        self.units = "unknown"
        self.state = "RUN"
        self.trigger_kind: str | None = None
        self.held: InstrumentSnapshot | None = None
        self.event_evictions = 0
        self.continuity_resets = 0
        self.last_frame: int | None = None
        self.last_loss: int | None = None

    def _now(self, value: float | None) -> float:
        value = time.monotonic() if value is None else float(value)
        if not np.isfinite(value) or value < 0:
            raise ValueError("PC时间必须有限且非负")
        if self.events and value < self.events[-1].receive_monotonic_s:
            raise ValueError("PC事件时间不能倒退")
        return value

    def observe_frame(self, frame: AcousticFrame, now: float | None = None) -> None:
        frame.validate()
        if frame.evidence_source != self.source:
            raise ValueError("不能混合不同来源；切换会话后再观察")
        rate = float(frame.sample_rate_hz)
        if not np.isfinite(rate) or not 1000 <= rate <= 192000 or not 2 <= frame.pcm.shape[0] <= 65536:
            raise ValueError("观察器采样率或帧长越界")
        if frame.frame_index < 0 or frame.packet_loss < 0:
            raise ValueError("帧号/丢包计数不能为负")
        stamp = self._now(now)
        raw = frame.raw_pcm_i32
        units = "int32 PCM counts" if raw is not None else "normalized float PCM"
        changed = self.units != "unknown" and (rate != self.sample_rate_hz or units != self.units)
        gap = self.last_frame is not None and (
            frame.frame_index != self.last_frame + 1 or frame.packet_loss != self.last_loss)
        if changed or gap:
            self.chunks.clear()
            self.sample_count = 0
            self.continuity_resets += 1
            # 先添加新帧再发送错误事件，使缺口触发只包含
            # 新的连续片段，不跨缺口拼接。
        self.sample_rate_hz, self.units = rate, units
        samples = np.array(raw if raw is not None else frame.pcm, copy=True)
        capacity = int(np.ceil(rate))
        samples = samples[-capacity:]
        self.chunks.append(samples)
        self.sample_count += samples.shape[0]
        while self.sample_count > capacity:
            excess = self.sample_count - capacity
            first = self.chunks.popleft()
            if len(first) > excess:
                self.chunks.appendleft(first[excess:].copy())
                self.sample_count -= excess
            else:
                self.sample_count -= len(first)
        self.last_frame, self.last_loss = frame.frame_index, frame.packet_loss
        if changed or gap:
            self.observe_event("ERROR", frame.frame_index, "PCM连续段重置：来源内缺口/帧号/采样率/单位变化", stamp)
        self.observe_event("AUDIO", frame.frame_index, f"{frame.pcm.shape[0]} samples · {units}", stamp)

    def observe_event(self, kind: str, frame_id: int | None = None, detail: str = "", now: float | None = None) -> None:
        if kind not in EVENT_KINDS or (frame_id is not None and frame_id < 0):
            raise ValueError("事件类型或帧号不合法")
        stamp = self._now(now)
        if len(self.events) == self.event_capacity:
            self.event_evictions += 1
        self.events.append(InstrumentEvent(stamp, kind, frame_id, str(detail)[:512]))
        if self.state == "ARMED" and kind == self.trigger_kind:
            self.state = "TRIGGERED"
            self.held = self._capture(stamp)

    def _capture(self, now: float) -> InstrumentSnapshot:
        samples = np.concatenate(self.chunks) if self.chunks else np.empty((0, 16), dtype=np.float32)
        samples.setflags(write=False)
        return InstrumentSnapshot(samples, self.sample_rate_hz, self.units, self.source,
            self.session_id, now, tuple(self.events), self.state, self.event_evictions, self.continuity_resets)

    def snapshot(self, now: float | None = None) -> InstrumentSnapshot:
        return self.held if self.held is not None else self._capture(self._now(now))

    def toggle_hold(self, now: float | None = None) -> None:
        stamp = self._now(now)
        self.trigger_kind = None
        if self.held is not None:
            self.state, self.held = "RUN", None
        else:
            self.state = "HOLD"
            self.held = self._capture(stamp)

    def arm(self, kind: str) -> None:
        if kind not in ("AUDIO", "RESULT", "ENHANCED", "ERROR"):
            raise ValueError("触发源不合法")
        self.state, self.trigger_kind, self.held = "ARMED", kind, None

    def clear_events(self) -> None:
        # 沿用原语义：恢复时保留音频、清空事件历史。
        self.events.clear()
        self.event_evictions = 0
        self.state, self.trigger_kind, self.held = "RUN", None, None


def export_instrument_snapshot(snapshot: InstrumentSnapshot, output: Path, channel: int,
                               timebase_ms: int, save_png) -> Path:
    """使用独占新目录，CSV与PNG必须来自同一保持快照。"""
    axis, pcm = snapshot.channel_window(channel, timebase_ms)
    if not pcm.size and not snapshot.events:
        raise ValueError("尚无PCM或事件，不能导出空证据")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    pcm_path, event_path = output / "pcm.csv", output / "events.csv"
    with pcm_path.open("x", newline="", encoding="utf-8-sig") as stream:
        writer = csv.writer(stream)
        writer.writerow(("time_ms", "channel", "pcm"))
        writer.writerows((repr(float(t)), f"CH{channel:02d}", int(v) if snapshot.units == "int32 PCM counts" else repr(float(v)))
                         for t, v in zip(axis, pcm, strict=True))
    with event_path.open("x", newline="", encoding="utf-8-sig") as stream:
        writer = csv.writer(stream)
        writer.writerow(("relative_time_ms", "event", "frame_id", "detail"))
        writer.writerows(((e.receive_monotonic_s - snapshot.reference_monotonic_s) * 1000,
                          e.kind, "" if e.frame_id is None else e.frame_id, e.detail) for e in snapshot.events)
    if not save_png(output / "instrument.png"):
        raise OSError("PNG保存失败；目录为未完成导出，不生成完成清单")
    meta = {"schema": "mic16.qt.virtual_instrument.v1", "source": snapshot.source,
        "observer_session_id": snapshot.session_id, "state": snapshot.state,
        "channel_zero_based": channel, "timebase_ms": timebase_ms, "pcm_units": snapshot.units,
        "sample_rate_hz": snapshot.sample_rate_hz if snapshot.units != "unknown" else None, "samples": len(pcm), "events": len(snapshot.events),
        "event_evictions": snapshot.event_evictions, "continuity_resets": snapshot.continuity_resets,
        "reference_pc_monotonic_s": snapshot.reference_monotonic_s,
        "display_dc_removed": True, "csv_dc_removed": False,
        "boundary_zh": "PC接受数据/界面派发事件，非FPGA内部ILA。PCM横轴相对最后样本，事件横轴相对PC观察时刻，两者不保证样本级同步。BOARD LIVE为协议来源，不是实板验证认证；合成/回放不变成实测。"}
    (output / "metadata.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    files = sorted(p for p in output.iterdir() if p.is_file())
    (output / "manifest.json").write_text(json.dumps({"schema": "mic16.qt.instrument.export.v1",
        "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}, indent=2), encoding="utf-8")
    return output
