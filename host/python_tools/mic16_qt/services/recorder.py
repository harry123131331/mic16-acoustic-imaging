from __future__ import annotations

import csv
import hashlib
import json
import ipaddress
import struct
import re
import subprocess
import sys
import wave
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

import numpy as np

from mic16_qt.core.models import AcousticFrame
from mic16_qt.services.board_protocol import WdasDiagnosticsReceipt, BoardProtocolAccumulator
from mic16_udp_common import normalize_to_int16, parse_wdas_diagnostics_payload
from mic16_qt.services.recording_labels import RecordingLabels


@dataclass(frozen=True)
class RecordingArtifacts:
    session_dir: Path
    raw_npy: Path
    metadata_json: Path
    frame_manifest_csv: Path
    segment_manifest_csv: Path
    session_manifest_json: Path
    sample_count: int
    frame_count: int
    sample_rate_hz: float


class PcmSessionRecorder:
    """追加已验证int32帧，保存与旧格式兼容的文件。录制中写临时平面PCM流，时长不受内存容量直接限制；CSV以帧为粒度，累计丢包数不能替代每个原始AUDIO包的逐包记录。"""

    def __init__(self, output_root: Path, app_version: str) -> None:
        self.output_root = Path(output_root).resolve()
        self.app_version = str(app_version)
        self._stream = None
        self._temporary_raw: Path | None = None
        self._session_dir: Path | None = None
        self._prefix = ""
        self._source = ""
        self._sample_rate_hz = 0.0
        self._channel_count = 16
        self._frame_count = 0
        self._segment_count = 0
        self._last_frame_id = None
        self._manifest_stream = self._segments_stream = None
        self._labels: RecordingLabels | None = None
        self._sample_count = 0
        self._last_loss = 0
        self._segment_id = 0
        self._segment_start = 0
        self._diagnostic_stream = None
        self._diagnostic_count = 0
        self._diagnostic_endpoint = None
        self._diagnostic_listener = None
        self._diagnostic_sequence = None
        self._diagnostic_write_error = None

    @property
    def active(self) -> bool:
        return self._stream is not None

    @property
    def sample_count(self) -> int:
        return self._sample_count

    @staticmethod
    def validate_start(evidence_source: str, sample_rate_hz: float, prefix: str) -> None:
        if evidence_source not in {"REPLAY", "BOARD LIVE", "PC LIVE"}:
            raise ValueError("只能录制 REPLAY、BOARD LIVE 或 PC LIVE 原始 PCM")
        if not np.isfinite(sample_rate_hz) or sample_rate_hz <= 0:
            raise ValueError("采样率必须为有限正数")
        if prefix and (not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]{0,127}", prefix)
                or prefix.endswith(".") or prefix.split(".")[0].upper() in
                {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}):
            raise ValueError("录制名称无效：限128个ASCII字符，不能为路径或Windows保留名称")

    def start(self, *, evidence_source: str, sample_rate_hz: float, prefix: str = "", labels: RecordingLabels | None = None) -> Path:
        if self.active:
            raise RuntimeError("录制已经开始")
        self.validate_start(evidence_source, sample_rate_hz, prefix.strip())
        if labels is not None:
            labels.validate()
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._prefix = prefix.strip() or f"mic16_qt_{stamp}"
        self._session_dir = self.output_root / self._prefix
        self._session_dir.mkdir(parents=True, exist_ok=False)
        self._temporary_raw = self._session_dir / f"{self._prefix}.recording.pcm"
        try:
            self._stream = self._temporary_raw.open("xb")
            self._manifest_stream = (self._session_dir / "packet_manifest.csv").open("x", newline="", encoding="utf-8")
            self._segments_stream = (self._session_dir / f"{self._prefix}_segments.csv").open("x", newline="", encoding="utf-8")
            self._frame_writer = csv.DictWriter(self._manifest_stream, fieldnames=[
                "receive_index", "frame_id", "expected_frame_id", "packet_status", "gap_count", "duplicate_flag",
                "out_of_order_flag", "segment_id", "sample_start", "sample_end", "source_ip", "source_port",
                "datagram_bytes", "checksum_status"])
            self._frame_writer.writeheader()
            self._segment_writer = csv.DictWriter(self._segments_stream, fieldnames=list(self._segment_row(0, 0, 0, lost_before_next=0)))
            self._segment_writer.writeheader()
        except Exception:
            self.abort()
            raise
        self._source = str(evidence_source)
        self._sample_rate_hz = float(sample_rate_hz)
        self._frame_count = self._segment_count = 0
        self._last_frame_id = None
        self._labels = labels
        self._sample_count = 0
        self._last_loss = 0
        self._segment_id = 0
        self._segment_start = 0
        self._diagnostic_count = 0
        self._diagnostic_endpoint = None
        self._diagnostic_listener = None
        self._diagnostic_sequence = None
        self._diagnostic_write_error = None
        return self._session_dir

    def append_diagnostics(self, receipt: WdasDiagnosticsReceipt) -> bool:
        """以有界内存保存已接受快照，不推测缺口。接收时刻与样本数只对齐电脑观测流，不能把固件计数变化关联到特定PCM帧；回放或纯状态会话不得标成实板结果。"""
        if not self.active or self._source != "BOARD LIVE":
            return False
        if not isinstance(receipt, WdasDiagnosticsReceipt):
            raise ValueError("需要经过协议校验的诊断接收记录")
        if (ipaddress.ip_address(receipt.source_endpoint[0]).version != 4
                or not 1 <= receipt.source_endpoint[1] <= 65535
                or not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", receipt.listener_session_id)
                or receipt.transport_origin not in {"UDP_RECEIVED_NOT_BOARD_VERIFIED", "HOST_C_WIRE_MOCK_NOT_BOARD"}
                or receipt.received_monotonic_ns < 0
                or datetime.fromisoformat(receipt.received_at_utc).utcoffset() is None):
            raise ValueError("诊断来源或接收时间信息无效")
        if (self._diagnostic_listener is not None and
                (receipt.listener_session_id != self._diagnostic_listener
                 or receipt.source_endpoint != self._diagnostic_endpoint)):
            raise ValueError("诊断监听会话或发送端变化，请先结束录制")
        d = receipt.diagnostics
        parse_wdas_diagnostics_payload(struct.pack("<32I", *asdict(d).values()))
        if BoardProtocolAccumulator._is_old(d.sequence, self._diagnostic_sequence, reject_equal=True):
            return False
        row = {
            "schema_version": "mic16.qt.wdas_diagnostics_receipt.v1",
            "transport_origin": receipt.transport_origin,
            "received_at_utc": receipt.received_at_utc,
            "received_monotonic_ns": receipt.received_monotonic_ns,
            "listener_session_id": receipt.listener_session_id,
            "source_ip": receipt.source_endpoint[0], "source_port": receipt.source_endpoint[1],
            "recorded_pcm_samples_at_receive": self._sample_count,
            "record_index": self._diagnostic_count,
            "snapshot": asdict(d),
        }
        encoded = json.dumps(row, ensure_ascii=False, allow_nan=False) + "\n"
        try:
            if self._diagnostic_stream is None:
                self._diagnostic_stream = (self._session_dir / "board_diagnostics.jsonl").open("x", encoding="utf-8")
            self._diagnostic_stream.write(encoded)
            self._diagnostic_stream.flush()
        except OSError as exc:
            self._diagnostic_write_error = str(exc)
            raise
        self._diagnostic_count += 1
        self._diagnostic_listener = receipt.listener_session_id
        self._diagnostic_endpoint = receipt.source_endpoint
        self._diagnostic_sequence = d.sequence
        return True

    def append(self, frame: AcousticFrame) -> bool:
        if not self.active:
            return False
        if frame.raw_pcm_i32 is None:
            return False
        if frame.evidence_source != self._source:
            raise ValueError("录制过程中数据来源发生变化，请先结束当前会话")
        raw = np.asarray(frame.raw_pcm_i32)
        if raw.dtype != np.int32 or raw.ndim != 2 or raw.shape[1] != self._channel_count:
            raise ValueError("录制输入必须是 int32 [N,16]")
        if not np.isclose(float(frame.sample_rate_hz), self._sample_rate_hz):
            raise ValueError("录制过程中采样率发生变化")

        loss = int(frame.packet_loss)
        loss_delta = max(0, loss - self._last_loss)
        if loss_delta > 0 and self._sample_count > self._segment_start:
            self._segment_writer.writerow(
                self._segment_row(
                    self._segment_id,
                    self._segment_start,
                    self._sample_count,
                    lost_before_next=loss_delta,
                )
            )
            self._segment_count += 1
            self._segment_id += 1
            self._segment_start = self._sample_count

        start = self._sample_count
        raw.astype("<i4", copy=False).tofile(self._stream)
        self._sample_count += int(raw.shape[0])
        self._frame_writer.writerow(
            {
                "receive_index": self._frame_count,
                "frame_id": int(frame.frame_index),
                "expected_frame_id": (
                    "" if self._last_frame_id is None else self._last_frame_id + 1
                ),
                "packet_status": "assembled_frame",
                "gap_count": loss_delta,
                "duplicate_flag": 0,
                "out_of_order_flag": 0,
                "segment_id": self._segment_id,
                "sample_start": start,
                "sample_end": self._sample_count,
                "source_ip": self._source,
                "source_port": "",
                "datagram_bytes": "",
                "checksum_status": "verified_upstream",
            }
        )
        self._frame_count += 1
        self._last_frame_id = int(frame.frame_index)
        self._last_loss = loss
        return True

    def abort(self) -> None:
        """关闭本模块拥有的句柄，IO失败时保留可恢复文件。"""
        for name in ("_stream", "_manifest_stream", "_segments_stream", "_diagnostic_stream"):
            stream = getattr(self, name, None)
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass
                setattr(self, name, None)

    def stop(self, *, completion_error: str = "", writer_statistics: dict | None = None) -> RecordingArtifacts:
        if not self.active or self._session_dir is None or self._temporary_raw is None:
            raise RuntimeError("当前没有录制任务")
        if self._sample_count <= 0:
            self.abort()
            self._temporary_raw.unlink(missing_ok=True)
            (self._session_dir / "packet_manifest.csv").unlink(missing_ok=True)
            (self._session_dir / f"{self._prefix}_segments.csv").unlink(missing_ok=True)
            try:
                self._session_dir.rmdir()
            except OSError:
                pass
            raise RuntimeError("录制期间没有收到可保存的 16 通道 PCM")
        self._segment_writer.writerow(
            self._segment_row(
                self._segment_id,
                self._segment_start,
                self._sample_count,
                lost_before_next=0,
            )
        )
        self._segment_count += 1
        # 刷新失败必须保留为错误，不能标成完成录制。
        try:
            for stream in (self._stream, self._manifest_stream, self._segments_stream, self._diagnostic_stream):
                if stream is not None:
                    stream.flush()
        finally:
            self.abort()

        raw_npy = self._session_dir / f"{self._prefix}_raw_i32.npy"
        source = np.memmap(
            self._temporary_raw,
            dtype="<i4",
            mode="r",
            shape=(self._sample_count, self._channel_count),
        )
        target = np.lib.format.open_memmap(
            raw_npy,
            mode="w+",
            dtype=np.int32,
            shape=source.shape,
        )
        block = 65_536
        for start in range(0, self._sample_count, block):
            target[start : start + block] = source[start : start + block]
        target.flush()
        del target
        del source
        self._temporary_raw.unlink()

        frame_manifest = self._session_dir / "packet_manifest.csv"
        segment_manifest = self._session_dir / f"{self._prefix}_segments.csv"
        metadata = {
            "schema_version": "mic16.qt.recording.v1",
            "created_at": datetime.now().isoformat(timespec="seconds"),
            "app_version": self.app_version,
            "evidence_source": self._source,
            "sample_rate_hz": self._sample_rate_hz,
            "channel_count": self._channel_count,
            "sample_count": self._sample_count,
            "frame_count": self._frame_count,
            "duration_s": self._sample_count / self._sample_rate_hz,
            "lost_packets_observed": self._last_loss,
            "continuous_segment_count": self._segment_count,
            "recording_outcome": {"status": "INCOMPLETE" if completion_error else "COMPLETED",
                                  "error": completion_error, "writer_statistics": writer_statistics or {}},
            "capture_labels": self._labels.metadata() if self._labels else None,
            "capture_valid": False,
            "valid_for_training": False,
            "capture_validation_status": "NOT_AUDITED_NO_AUTOMATIC_DATASET_ADMISSION",
            "manifest_granularity": "assembled_pcm_frame",
            "raw_layout": "samples x channels; original UDP channel order",
            "raw_dtype": "int32",
            "board_diagnostics": {
                "availability": ("write_failed_incomplete" if self._diagnostic_write_error else
                                 "received" if self._diagnostic_count else "unknown_not_received"),
                "snapshot_count": self._diagnostic_count,
                "file": "board_diagnostics.jsonl" if self._diagnostic_count else None,
                "listener_session_id": self._diagnostic_listener,
                "write_error": self._diagnostic_write_error,
                "claim_boundary_zh": "接收端周期快照；不等于实板认证，不提供逐帧缺口归因，不推断设备重启或计数差分。",
            },
            "claim_boundary": (
                "Qt Q4-A recording evidence. Frame-level continuity is preserved; "
                "use the standalone packet recorder for per-datagram forensic evidence."
            ),
        }
        metadata_json = self._session_dir / f"{self._prefix}_metadata.json"
        metadata_json.write_text(
            json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        session_manifest = self._session_dir / "session_manifest.json"
        files = [raw_npy, metadata_json, frame_manifest, segment_manifest]
        if self._diagnostic_count:
            files.append(self._session_dir / "board_diagnostics.jsonl")
        session_manifest.write_text(
            json.dumps(
                {
                    **metadata,
                    "files": [
                        {
                            "name": path.name,
                            "sha256": self._sha256(path),
                            "bytes": path.stat().st_size,
                        }
                        for path in files
                    ],
                },
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        return RecordingArtifacts(
            session_dir=self._session_dir,
            raw_npy=raw_npy,
            metadata_json=metadata_json,
            frame_manifest_csv=frame_manifest,
            segment_manifest_csv=segment_manifest,
            session_manifest_json=session_manifest,
            sample_count=self._sample_count,
            frame_count=self._frame_count,
            sample_rate_hz=self._sample_rate_hz,
        )

    @staticmethod
    def export_wavs(artifacts: RecordingArtifacts, sample_rate_hz: float) -> list[Path]:
        raw = np.load(artifacts.raw_npy, mmap_mode="r")
        paths: list[Path] = []
        channel_dir = artifacts.session_dir / "channel_wavs"
        channel_dir.mkdir(exist_ok=True)
        for channel in range(raw.shape[1]):
            path = channel_dir / f"ch{channel:02d}.wav"
            mono = normalize_to_int16(np.asarray(raw[:, channel : channel + 1]))[:, 0]
            with wave.open(str(path), "wb") as stream:
                stream.setnchannels(1)
                stream.setsampwidth(2)
                stream.setframerate(int(round(sample_rate_hz)))
                stream.writeframes(mono.tobytes(order="C"))
            paths.append(path)
        return paths

    @staticmethod
    def export_matlab(artifacts: RecordingArtifacts, sample_rate_hz: float) -> Path:
        exporter = Path(__file__).resolve().parents[2] / "mic16_export_matlab.py"
        output = artifacts.session_dir / f"{artifacts.raw_npy.stem.removesuffix('_raw_i32')}.mat"
        metadata = json.loads(artifacts.metadata_json.read_text(encoding="utf-8"))
        labels = metadata.get("capture_labels") or {}
        subprocess.run(
            [
                sys.executable,
                str(exporter),
                "--input",
                str(artifacts.raw_npy),
                "--metadata",
                str(artifacts.metadata_json),
                "--out",
                str(output),
                "--sample-rate",
                str(sample_rate_hz),
                "--signal-type",
                str(labels.get("signal_type", "unknown")),
                "--description",
                "PySide6 Q4-A session export",
            ],
            check=True,
            cwd=str(exporter.parent),
            timeout=120,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        return output

    @staticmethod
    def refresh_session_manifest(artifacts: RecordingArtifacts) -> None:
        metadata = json.loads(artifacts.metadata_json.read_text(encoding="utf-8"))
        files = sorted(
            (
                path
                for path in artifacts.session_dir.rglob("*")
                if path.is_file() and path != artifacts.session_manifest_json
            ),
            key=lambda path: path.as_posix(),
        )
        artifacts.session_manifest_json.write_text(
            json.dumps(
                {
                    **metadata,
                    "files": [
                        {
                            "name": path.relative_to(artifacts.session_dir).as_posix(),
                            "sha256": PcmSessionRecorder._sha256(path),
                            "bytes": path.stat().st_size,
                        }
                        for path in files
                    ],
                },
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

    @staticmethod
    def _write_csv(path: Path, rows: list[dict[str, object]]) -> None:
        if not rows:
            raise ValueError(f"cannot write empty manifest: {path}")
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

    @staticmethod
    def _segment_row(
        segment_id: int,
        start: int,
        end: int,
        *,
        lost_before_next: int,
    ) -> dict[str, object]:
        return {
            "segment_id": segment_id,
            "sample_start": start,
            "sample_end": end,
            "start_sample": start,
            "end_sample": end,
            "sample_count": end - start,
            "lost_packets_before_next_segment": lost_before_next,
            "continuous": 1,
        }

    @staticmethod
    def _sha256(path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()
