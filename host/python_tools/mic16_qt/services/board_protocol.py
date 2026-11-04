from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from mic16_config import OFFICIAL_SAMPLE_RATE
from mic16_qt.core.models import (
    AcousticFrame,
    EnhancedAudioUpdate,
    LocalizationUpdate,
    SourceEstimate,
)
from mic16_qt.services.audio_sources import AudioFrameFactory, UdpAudioAccumulator
from mic16_udp_common import (
    FRAME_TYPE_AUDIO,
    FRAME_TYPE_ENHANCED_AUDIO,
    FRAME_TYPE_MULTISOURCE_RESULT,
    FRAME_TYPE_STATUS, FRAME_TYPE_RTOS_STATUS, FRAME_TYPE_COMMAND_ACK,
    FRAME_TYPE_WDAS_DIAGNOSTICS,
    Mic16Status, Mic16RtosStatus, Mic16CommandAck, Mic16WdasDiagnostics,
    MULTISOURCE_ALGORITHM_TF_ZONE_V1,
    Mic16EnhancedAudioAssembler,
    parse_enhanced_audio_chunk,
    parse_mic16_frame,
    parse_multisource_result_payload,
    parse_status_payload, parse_rtos_status_payload, parse_command_ack_payload,
    parse_wdas_diagnostics_payload,
)


BoardEvent = AcousticFrame | LocalizationUpdate | EnhancedAudioUpdate | Mic16Status | Mic16RtosStatus | Mic16CommandAck | Mic16WdasDiagnostics


@dataclass(frozen=True)
class WdasDiagnosticsReceipt:
    diagnostics: Mic16WdasDiagnostics
    source_endpoint: tuple[str, int]
    received_at_utc: str
    received_monotonic_ns: int
    listener_session_id: str
    transport_origin: str = "UDP_RECEIVED_NOT_BOARD_VERIFIED"


class BoardProtocolAccumulator:
    """解码带版本的板端协议，不补造缺失证据。原始音频、多源结果和增强音频分别作为独立事件；固件可单独发送定位或增强，因此GUI不能为统一显示模型而伪造原始波形。"""

    def __init__(self, frame_samples: int = 512) -> None:
        self.audio = UdpAudioAccumulator(frame_samples=frame_samples)
        self.enhanced = Mic16EnhancedAudioAssembler()
        self.parse_error_count = 0
        self.ignored_packet_count = 0
        self.localization_packet_count = 0
        self.enhanced_chunk_count = 0
        self.enhanced_frame_count = 0
        self.enhanced_missing_frames = 0
        self.enhanced_partial_discards = 0
        self.source_mismatch_count = 0
        self.stale_result_count = 0
        self._source: tuple[str, int] | None = None
        self._last_localization_id: int | None = None
        self._last_enhanced_id: int | None = None
        self._completed_enhanced_id: int | None = None
        self._last_status_sequence: int | None = None
        self._last_diagnostics_sequence: int | None = None
        self._control_generation: int | None = None

    def push_datagram(self, datagram: bytes, source: tuple[str, int]) -> list[BoardEvent]:
        frame, error = parse_mic16_frame(datagram, source=source, strict_checksum=True)
        if frame is None:
            self.parse_error_count += 1
            return []
        if self._source is not None and source != self._source:
            self.source_mismatch_count += 1
            return []
        try:
            if frame.header.frame_type in (FRAME_TYPE_STATUS, FRAME_TYPE_RTOS_STATUS, FRAME_TYPE_COMMAND_ACK, FRAME_TYPE_WDAS_DIAGNOSTICS):
                if frame.header.sample_count or frame.header.channel_count or frame.header.sample_bits:
                    raise ValueError("control/status header must have zero sample declarations")
                if frame.header.frame_type == FRAME_TYPE_STATUS:
                    if len(frame.payload) != 44:
                        raise ValueError("STATUS payload length")
                    update = parse_status_payload(frame.payload)
                    inner_id = update.frame_id
                elif frame.header.frame_type == FRAME_TYPE_RTOS_STATUS:
                    update = parse_rtos_status_payload(frame.payload)
                    inner_id = update.sequence
                    if inner_id != frame.header.frame_id:
                        raise ValueError("RTOS_STATUS outer/inner ID mismatch")
                    if (self._is_old(inner_id, self._last_status_sequence, reject_equal=True)
                            or self._is_old(inner_id, self._last_diagnostics_sequence, reject_equal=False)):
                        self.stale_result_count += 1
                        return []
                    if (self._control_generation is not None
                            and self._control_generation != update.requested_generation):
                        self.audio.discard_partial()
                        if self.enhanced._frame_id is not None:
                            self.enhanced_partial_discards += 1
                        self.enhanced = Mic16EnhancedAudioAssembler()
                    self._last_status_sequence = inner_id
                    self._control_generation = update.requested_generation
                elif frame.header.frame_type == FRAME_TYPE_WDAS_DIAGNOSTICS:
                    update = parse_wdas_diagnostics_payload(frame.payload)
                    inner_id = update.sequence
                    if inner_id != frame.header.frame_id:
                        raise ValueError("WDAS_DIAGNOSTICS outer/inner ID mismatch")
                    if (self._is_old(inner_id, self._last_diagnostics_sequence, reject_equal=True)
                            or self._is_old(inner_id, self._last_status_sequence, reject_equal=False)):
                        self.stale_result_count += 1
                        return []
                    # 诊断不切换模式或清空音频缓冲区。
                    self._last_diagnostics_sequence = inner_id
                else:
                    update = parse_command_ack_payload(frame.payload)
                    inner_id = update.request_id
                if inner_id != frame.header.frame_id:
                    raise ValueError("control/status outer/inner ID mismatch")
                self._source = source
                return [update]
            if frame.header.frame_type == FRAME_TYPE_AUDIO:
                events = list(self.audio.push_datagram(datagram, source))
                # 异常AUDIO包不能抢占端点绑定。
                if self.audio.source_endpoint is not None:
                    self._source = source
                return events
            if frame.header.frame_type == FRAME_TYPE_MULTISOURCE_RESULT:
                update = self._localization_update(frame.payload)
                if update.frame_index != frame.header.frame_id:
                    raise ValueError("MULTISOURCE_RESULT outer/inner frame_id mismatch")
                if self._is_old(update.frame_index, self._last_localization_id, reject_equal=True):
                    self.stale_result_count += 1
                    return []
                self._source = source
                self._last_localization_id = update.frame_index
                self.localization_packet_count += 1
                return [update]
            if frame.header.frame_type == FRAME_TYPE_ENHANCED_AUDIO:
                chunk = parse_enhanced_audio_chunk(frame)
                if (self._is_old(chunk.frame_id, self._last_enhanced_id, reject_equal=False)
                        or chunk.frame_id == self._completed_enhanced_id):
                    self.stale_result_count += 1
                    return []
                pending_frame_id = self.enhanced._frame_id
                completed = self.enhanced.push(chunk)
                if self._last_enhanced_id is not None and chunk.frame_id != self._last_enhanced_id:
                    step = (chunk.frame_id - self._last_enhanced_id) & 0xFFFFFFFF
                    self.enhanced_missing_frames += step - 1
                    if pending_frame_id is not None and pending_frame_id != chunk.frame_id:
                        self.enhanced_partial_discards += 1
                self._source = source
                self._last_enhanced_id = chunk.frame_id
                self.enhanced_chunk_count += 1
                if completed is None:
                    return []
                self.enhanced_frame_count += 1
                self._completed_enhanced_id = chunk.frame_id
                return [self._enhanced_update(chunk.frame_id, completed)]
        except ValueError:
            self.parse_error_count += 1
            return []
        self.ignored_packet_count += 1
        return []

    @staticmethod
    def _is_old(frame_id: int, previous: int | None, *, reject_equal: bool) -> bool:
        if previous is None:
            return False
        delta = (int(frame_id) - int(previous)) & 0xFFFFFFFF
        return delta >= 0x80000000 or (reject_equal and delta == 0)

    @staticmethod
    def _localization_update(payload: bytes) -> LocalizationUpdate:
        result = parse_multisource_result_payload(payload)
        algorithm_name = (
            "Frozen v1 / TF-zone"
            if result.algorithm_version == MULTISOURCE_ALGORITHM_TF_ZONE_V1
            else f"Board algorithm 0x{result.algorithm_version:08X}"
        )
        sources: list[SourceEstimate] = []
        for index, item in enumerate(result.sources):
            level_margin = float(item.level_gate_margin_q16) / 65536.0
            entropy_margin = float(item.entropy_gate_margin_q16) / 65536.0
            sources.append(
                SourceEstimate(
                    source_id=f"S{index + 1}",
                    azimuth_deg=float(item.azimuth_deg_x100) / 100.0,
                    elevation_deg=float(item.elevation_deg_x100) / 100.0,
                    evidence_db=min(level_margin, entropy_margin),
                    state="ACCEPTED",
                    evidence_text=(
                        f"R{item.candidate_rank} · "
                        f"L {level_margin:+.2f} / H {entropy_margin:+.2f}"
                    ),
                )
            )
        update = LocalizationUpdate(
            frame_index=int(result.frame_id),
            sources=tuple(sources),
            candidate_count=int(result.candidate_count),
            latency_ms=float(result.latency_us) / 1000.0,
            algorithm_name=algorithm_name,
            calibration_crc32=int(result.calibration_crc32),
            flags=int(result.flags),
        )
        update.validate()
        return update

    @staticmethod
    def _enhanced_update(frame_index: int, raw_samples: np.ndarray) -> EnhancedAudioUpdate:
        raw = np.asarray(raw_samples, dtype=np.int32).copy()
        samples = (raw.astype(np.float64) / float(1 << 31)).astype(np.float32)
        window = np.hanning(samples.size).astype(np.float32)
        frequency_hz = np.fft.rfftfreq(samples.size, 1.0 / float(OFFICIAL_SAMPLE_RATE))
        spectrum_db = AudioFrameFactory._spectrum_db(samples * window)
        rms = float(np.sqrt(np.mean(np.square(samples.astype(np.float64)))))
        update = EnhancedAudioUpdate(
            frame_index=int(frame_index),
            sample_rate_hz=float(OFFICIAL_SAMPLE_RATE),
            raw_samples_i32=raw,
            samples=samples,
            frequency_hz=frequency_hz.astype(np.float32),
            spectrum_db=spectrum_db,
            output_rms_dbfs=20.0 * math.log10(max(rms, 1e-12)),
        )
        update.validate()
        return update
