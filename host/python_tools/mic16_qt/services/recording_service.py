"""有界录制邮箱，文件IO全部在GUI线程外执行。停止时按序排空已接收帧；邮箱满则显式终止并保留未完成前缀，不能静默跳过或拼接样本。"""
from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
import json
from pathlib import Path
import queue
import threading
import time

import numpy as np
from PySide6.QtCore import QObject, QTimer, Signal

from mic16_qt.services.recorder import PcmSessionRecorder, RecordingArtifacts
from mic16_qt.services.recording_labels import RecordingLabels


@dataclass(frozen=True)
class RawRecordingFrame:
    frame_index: int
    sample_rate_hz: float
    evidence_source: str
    packet_loss: int
    raw_pcm_i32: np.ndarray


class RecordingService(QObject):
    progress_changed = Signal(object)
    finished = Signal(object)

    def __init__(self, parent=None, *, max_items=64, max_bytes=8 * 1024 * 1024):
        super().__init__(parent)
        if not 1 <= max_items <= 256 or not 1024 <= max_bytes <= 32 * 1024 * 1024:
            raise ValueError("录制队列预算无效")
        self.max_items, self.max_bytes = max_items, max_bytes
        self._lock = threading.Lock()
        self._thread = None
        self._busy = False
        self._accepting = False
        self._done = None
        self._state = "IDLE"
        self._timer = QTimer(self)
        self._timer.setInterval(50)
        self._timer.timeout.connect(self._poll)

    @property
    def busy(self):
        return self._busy

    @property
    def active(self):
        with self._lock:
            return self._busy and self._accepting

    @property
    def sample_count(self):
        with self._lock:
            return self._stats.get("accepted_samples", 0) if hasattr(self, "_stats") else 0

    @property
    def statistics(self):
        with self._lock:
            return dict(self._stats)

    @staticmethod
    def _outside_package(root: Path):
        for parent in (root, *root.parents):
            if (parent / "MANIFEST.sha256").is_file():
                raise ValueError("提交包只读：录制或导出输出请选择包外目录")

    def start(self, output_root, app_version, *, evidence_source, sample_rate_hz, prefix="", labels=None):
        if self.busy:
            raise ValueError("正在录制、保存或导出，请等待完成")
        PcmSessionRecorder.validate_start(evidence_source, sample_rate_hz, prefix.strip())
        if labels is not None:
            if not isinstance(labels, RecordingLabels):
                raise ValueError("录音标签类型无效")
            labels.validate()
        root = Path(output_root).resolve()
        self._outside_package(root)
        prefix = prefix.strip() or datetime.now().strftime("mic16_qt_%Y%m%d_%H%M%S_%f")
        self._queue = queue.Queue(maxsize=self.max_items)
        self._stop = threading.Event()
        self._error = ""
        self._source, self._sample_rate = evidence_source, float(sample_rate_hz)
        self._done = None
        self._state = "STARTING"
        self._stats = {"accepted_frames": 0, "accepted_samples": 0, "written_frames": 0,
                       "written_samples": 0, "queue_items": 0, "queue_bytes": 0,
                       "peak_queue_items": 0, "peak_queue_bytes": 0, "rejected_items": 0,
                       "enqueue_max_ms": 0.0, "max_items": self.max_items, "max_bytes": self.max_bytes}
        self._busy = self._accepting = True
        self._owned_session = False
        self.session_dir = root / prefix
        self._thread = threading.Thread(target=self._record, args=(root, app_version, prefix, labels), daemon=True)
        self._thread.start()
        self._timer.start()
        return self.session_dir

    def _reject(self, message):
        with self._lock:
            self._stats["rejected_items"] += 1
            self._error = self._error or message
            self._accepting = False
            self._state = "STOPPING"
        self._stop.set()
        return False

    def append(self, frame, *, session_token=None):
        with self._lock:
            if session_token is not None and session_token != str(getattr(self, "session_dir", "")):
                return False
        if not self.active or frame.raw_pcm_i32 is None:
            return False
        started = time.perf_counter()
        raw = np.asarray(frame.raw_pcm_i32)
        if (raw.dtype != np.int32 or raw.ndim != 2 or raw.shape[1] != 16 or not 1 <= raw.shape[0] <= 4096
                or frame.evidence_source != self._source or not np.isclose(frame.sample_rate_hz, self._sample_rate)
                or frame.packet_loss < 0):
            return self._reject("PCM形状、来源、采样率或连续性字段变化；录制已停止，不能拼接不同输入")
        copied = np.array(raw, dtype=np.int32, order="C", copy=True)
        copied.setflags(write=False)
        item = RawRecordingFrame(int(frame.frame_index), float(frame.sample_rate_hz), frame.evidence_source,
                                 int(frame.packet_loss), copied)
        accepted = self._put("pcm", item, copied.nbytes, session_token=session_token)
        with self._lock:
            self._stats["enqueue_max_ms"] = max(self._stats["enqueue_max_ms"], (time.perf_counter() - started) * 1000)
        return accepted

    def append_diagnostics(self, receipt, *, session_token=None):
        if not self.active or self._source != "BOARD LIVE":
            return False
        # 原写入器按顺序验证不可变回执。
        return self._put("diagnostic", receipt, 4096, session_token=session_token)

    def _put(self, kind, item, size, *, session_token=None):
        with self._lock:
            if not self._accepting or (session_token is not None and session_token != str(self.session_dir)):
                return False
            if self._stats["queue_bytes"] + size > self.max_bytes or self._queue.full():
                overflow = True
            else:
                overflow = False
                self._queue.put_nowait((kind, item, size))
                self._stats["queue_bytes"] += size
                self._stats["queue_items"] += 1
                for unit in ("bytes", "items"):
                    self._stats["peak_queue_" + unit] = max(self._stats["peak_queue_" + unit], self._stats["queue_" + unit])
                if kind == "pcm":
                    self._stats["accepted_frames"] += 1
                    self._stats["accepted_samples"] += item.raw_pcm_i32.shape[0]
        if overflow:
            return self._reject("磁盘录制队列达到上限；已停止接收，本次录音不完整，保留已接受数据")
        return True

    def stop(self):
        if not self.busy:
            return
        with self._lock:
            self._accepting = False
            if self._state != "EXPORTING":
                self._state = "STOPPING"
        if hasattr(self, "_stop"):
            self._stop.set()

    def _record(self, root, version, prefix, labels):
        recorder = PcmSessionRecorder(root, version)
        artifacts, error = None, ""
        try:
            recorder.start(evidence_source=self._source, sample_rate_hz=self._sample_rate, prefix=prefix, labels=labels)
            self._owned_session = True
            with self._lock:
                if self._accepting:
                    self._state = "RECORDING"
            while not (self._stop.is_set() and self._queue.empty()):
                try:
                    kind, item, size = self._queue.get(timeout=.02)
                except queue.Empty:
                    continue
                with self._lock:
                    self._stats["queue_bytes"] -= size
                    self._stats["queue_items"] -= 1
                if kind == "pcm":
                    recorder.append(item)
                    with self._lock:
                        self._stats["written_frames"] += 1
                        self._stats["written_samples"] += item.raw_pcm_i32.shape[0]
                else:
                    recorder.append_diagnostics(item)
            with self._lock:
                self._state = "SAVING"
                error, stats = self._error, dict(self._stats)
            artifacts = recorder.stop(completion_error=error, writer_statistics=stats)
        except Exception as exc:
            error = self._error or str(exc)
            recorder.abort()
        finally:
            # 失败后不保留未读PCM数组，已有文件保持完整。
            while not self._queue.empty():
                try:
                    self._queue.get_nowait()
                except queue.Empty:
                    break
            with self._lock:
                self._accepting = False
                stats = dict(self._stats)
            result = {"action": "record", "ok": artifacts is not None and not error,
                      "artifacts": artifacts, "error": error, "session_dir": str(self.session_dir), "statistics": stats}
            self._write_receipt(result)
            with self._lock:
                self._done = result

    def export(self, artifacts: RecordingArtifacts):
        if self.busy:
            raise ValueError("正在录制、保存或导出")
        self._outside_package(artifacts.session_dir.resolve())
        self._done = None
        self._busy = True
        self._accepting = False
        self._state = "EXPORTING"
        self._stats = {}
        self.session_dir = artifacts.session_dir
        self._owned_session = True
        self._thread = threading.Thread(target=self._export, args=(artifacts,), daemon=True)
        self._thread.start()
        self._timer.start()

    def _export(self, artifacts):
        result = {"action": "export", "ok": False, "error": "", "artifacts": artifacts,
                  "session_dir": str(artifacts.session_dir), "statistics": {}}
        try:
            metadata = json.loads(artifacts.metadata_json.read_text(encoding="utf-8"))
            if metadata.get("recording_outcome", {}).get("status") != "COMPLETED":
                raise ValueError("不完整录音不可作为完整会话导出，请先检查恢复数据")
            PcmSessionRecorder.export_wavs(artifacts, artifacts.sample_rate_hz)
            PcmSessionRecorder.export_matlab(artifacts, artifacts.sample_rate_hz)
            PcmSessionRecorder.refresh_session_manifest(artifacts)
            result["ok"] = True
        except Exception as exc:
            result["error"] = str(exc)
        self._write_receipt(result)
        with self._lock:
            self._done = result

    def _write_receipt(self, result):
        try:
            if self._owned_session and self.session_dir.is_dir():
                # 回执单独保存，不属于被哈希的数据清单。
                target = self.session_dir.parent / (self.session_dir.name + "." + result["action"] + ".receipt.json")
                payload = {key: value for key, value in result.items() if key != "artifacts"}
                payload["schema_version"] = "mic16.qt.recording_job.v1"
                payload["boundary_zh"] = "主机录制/导出记录，不是声学性能或实板认证；失败文件保留，不能用于完整结果声明。"
                target.write_text(json.dumps(payload, ensure_ascii=False, allow_nan=False, indent=2), encoding="utf-8")
        except Exception as exc:
            result["ok"] = False
            result["error"] = result["error"] or f"任务回执保存失败：{exc}"

    def _poll(self):
        with self._lock:
            done = self._done
            progress = {"state": self._state, **self._stats}
        if done is not None:
            self._timer.stop()
            self._busy = False
            self._state = "IDLE"
            self._done = None
            self.finished.emit(done)
        elif self.busy:
            self.progress_changed.emit(progress)
