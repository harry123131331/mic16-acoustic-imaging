from __future__ import annotations

import math
from pathlib import Path

import numpy as np

from gui.workers.replay_worker import (
    find_manifest_segments,
    load_raw_i32,
    select_longest_segment,
)
from mic16_config import OFFICIAL_CHANNELS, OFFICIAL_SAMPLE_RATE, OFFICIAL_SAMPLES_PER_PACKET
from mic16_qt.core.models import AcousticFrame
from mic16_udp_common import is_non_audio_frame, parse_mic16_packet


def normalize_int32_pcm(raw_pcm_i32: np.ndarray) -> np.ndarray:
    """转换有符号int32 PCM，不改变通道或样本顺序。"""

    raw = np.asarray(raw_pcm_i32)
    if raw.ndim != 2 or raw.shape[1] != OFFICIAL_CHANNELS:
        raise ValueError(f"PCM 必须为 [N,{OFFICIAL_CHANNELS}]，实际为 {raw.shape}")
    if raw.dtype != np.int32:
        raw = raw.astype(np.int32, copy=False)
    return (raw.astype(np.float64) / float(1 << 31)).astype(np.float32)


class AudioFrameFactory:
    """从已验证的16路PCM构造仅供显示的帧，不虚构定位或增强结果。在接入带版本的结果数据前，响应图保持为空，比较波形使用通道1直通。"""

    def __init__(self, sample_rate_hz: float = OFFICIAL_SAMPLE_RATE) -> None:
        self.sample_rate_hz = float(sample_rate_hz)
        self.azimuth_axis_deg = np.linspace(-60.0, 60.0, 121, dtype=np.float32)
        self.elevation_axis_deg = np.linspace(-30.0, 30.0, 61, dtype=np.float32)

    def build(
        self,
        raw_pcm_i32: np.ndarray,
        *,
        frame_index: int,
        evidence_source: str,
        packet_loss: int = 0,
    ) -> AcousticFrame:
        raw = np.asarray(raw_pcm_i32, dtype=np.int32)
        pcm = normalize_int32_pcm(raw)
        sample_count = int(pcm.shape[0])
        if sample_count < 2:
            raise ValueError("PCM 帧至少需要 2 个采样点")
        passthrough = pcm[:, 0].copy()
        window = np.hanning(sample_count).astype(np.float32)
        frequency_hz = np.fft.rfftfreq(sample_count, 1.0 / self.sample_rate_hz)
        spectrum = self._spectrum_db(passthrough * window)
        frame = AcousticFrame(
            frame_index=int(frame_index),
            sample_rate_hz=self.sample_rate_hz,
            pcm=pcm,
            enhanced=passthrough,
            frequency_hz=frequency_hz.astype(np.float32),
            raw_spectrum_db=spectrum,
            enhanced_spectrum_db=spectrum.copy(),
            azimuth_axis_deg=self.azimuth_axis_deg,
            elevation_axis_deg=self.elevation_axis_deg,
            response_db=np.full(
                (self.elevation_axis_deg.size, self.azimuth_axis_deg.size),
                -30.0,
                dtype=np.float32,
            ),
            sources=(),
            candidates=(),
            packet_loss=int(packet_loss),
            latency_ms=0.0,
            input_rms_dbfs=self._rms_dbfs(passthrough),
            output_rms_dbfs=self._rms_dbfs(passthrough),
            snr_gain_db=0.0,
            algorithm_name="Q2 采集链路（定位与增强结果尚未接入）",
            evidence_source=evidence_source,
            raw_pcm_i32=raw.copy(),
        )
        frame.validate()
        return frame

    @staticmethod
    def _spectrum_db(signal: np.ndarray) -> np.ndarray:
        magnitude = np.abs(np.fft.rfft(signal))
        value = 20.0 * np.log10(np.maximum(magnitude, 1e-12))
        peak = float(np.max(value))
        if math.isfinite(peak):
            value -= peak
        return np.maximum(value, -80.0).astype(np.float32)

    @staticmethod
    def _rms_dbfs(signal: np.ndarray) -> float:
        rms = float(np.sqrt(np.mean(np.square(signal.astype(np.float64)))))
        return 20.0 * math.log10(max(rms, 1e-12))


class ReplayAcousticSource:
    """按原清单合同确定性回放raw_i32数据。"""

    def __init__(self, path: Path, frame_samples: int = 512) -> None:
        self.path = Path(path).resolve()
        self.raw_i32 = load_raw_i32(self.path, channels=OFFICIAL_CHANNELS)
        self.segment = select_longest_segment(
            self.raw_i32,
            find_manifest_segments(self.path.parent),
        )
        self.frame_samples = int(frame_samples)
        if self.segment.sample_count < self.frame_samples:
            raise ValueError(
                f"最长可用片段只有 {self.segment.sample_count} 点，少于 {self.frame_samples} 点"
            )
        self._cursor = int(self.segment.sample_start)
        self._frame_index = 0
        self._factory = AudioFrameFactory()

    def next_frame(self) -> AcousticFrame:
        if self._cursor + self.frame_samples > self.segment.sample_end:
            self._cursor = int(self.segment.sample_start)
        raw = self.raw_i32[self._cursor : self._cursor + self.frame_samples, :]
        frame = self._factory.build(
            raw,
            frame_index=self._frame_index,
            evidence_source="REPLAY",
        )
        self._cursor += self.frame_samples
        self._frame_index += 1
        return frame


class UdpAudioAccumulator:
    """Qt接收线程与黄金参考测试共用的纯协议层。"""

    def __init__(self, frame_samples: int = 512) -> None:
        self.frame_samples = int(frame_samples)
        if not 2 <= self.frame_samples <= 65_536:
            raise ValueError("组装帧长度必须在 2–65536 个采样点之间")
        self.packet_loss = 0
        self.packet_count = 0
        self.parse_error_count = 0
        self.duplicate_packet_count = 0
        self.out_of_order_packet_count = 0
        self.source_mismatch_count = 0
        self.discarded_partial_samples = 0
        self._source: tuple[str, int] | None = None
        self.last_packet_frame_id: int | None = None
        self._chunks: list[np.ndarray] = []
        self._sample_count = 0
        self._output_index = 0
        self._factory = AudioFrameFactory()

    @property
    def source_endpoint(self) -> tuple[str, int] | None:
        return self._source

    def discard_partial(self) -> None:
        """显式控制边界，不重置录制帧ID。"""
        self.discarded_partial_samples += self._sample_count
        self._chunks.clear()
        self._sample_count = 0

    def push_datagram(self, datagram: bytes, source: tuple[str, int]) -> list[AcousticFrame]:
        packet, error = parse_mic16_packet(
            datagram,
            source=source,
            channels=OFFICIAL_CHANNELS,
            samples_per_packet=OFFICIAL_SAMPLES_PER_PACKET,
            strict_checksum=True,
        )
        if packet is None:
            if not is_non_audio_frame(error):
                self.parse_error_count += 1
            return []
        # 一次监听只对应一个发送者及序号空间；重连时
        # 明确新建累积器，不拼接不同发送端。
        if self._source is None:
            self._source = source
        elif source != self._source:
            self.source_mismatch_count += 1
            return []
        current = int(packet.header.frame_id)
        if self.last_packet_frame_id is not None:
            advance = (current - self.last_packet_frame_id) & 0xFFFFFFFF
            if advance == 0:
                self.duplicate_packet_count += 1
                return []
            if advance >= 0x80000000:
                self.out_of_order_packet_count += 1
                return []
            if advance != 1:
                self.packet_loss += advance - 1
                # 显示/分析帧不能跨越网络缺口。
                self.discarded_partial_samples += self._sample_count
                self._chunks.clear()
                self._sample_count = 0
        self.last_packet_frame_id = int(packet.header.frame_id)
        self.packet_count += 1
        self._chunks.append(packet.samples.astype(np.int32, copy=False))
        self._sample_count += int(packet.samples.shape[0])

        frames: list[AcousticFrame] = []
        while self._sample_count >= self.frame_samples:
            merged = np.concatenate(self._chunks, axis=0)
            raw = merged[: self.frame_samples, :]
            remainder = merged[self.frame_samples :, :]
            self._chunks = [remainder] if remainder.size else []
            self._sample_count = int(remainder.shape[0]) if remainder.size else 0
            frames.append(
                self._factory.build(
                    raw,
                    frame_index=self._output_index,
                    evidence_source="BOARD LIVE",
                    packet_loss=self.packet_loss,
                )
            )
            self._output_index += 1
        return frames
