# -*- coding: utf-8 -*-
"""
MIC16 UDP common parser.

Frame format:
    24-byte header + variable payload + 4-byte checksum.

Raw AUDIO keeps the legacy 1024-byte payload and 1052-byte datagram. Result,
debug, parameter status, and enhanced-audio chunks use shorter payloads.

Header, little endian:
    uint32 magic
    uint16 version
    uint16 frame_type
    uint32 frame_id
    uint32 sample_count
    uint16 channel_count
    uint16 sample_bits
    uint32 payload_bytes
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Iterator, Optional, Tuple

import numpy as np

from mic16_config import OFFICIAL_CHANNELS, OFFICIAL_SAMPLE_RATE, OFFICIAL_SAMPLES_PER_PACKET


MAGIC = 0x55AA55AA
HEADER_BYTES = 24
CHECKSUM_BYTES = 4
# 正式帧为2048个PCM样本，同时兼容有界调试向量；
# 分配数组前先拒绝不支持的声明。
MAX_REASSEMBLY_WORDS = 65536

DEFAULT_CHANNELS = OFFICIAL_CHANNELS
DEFAULT_SAMPLES_PER_PACKET = OFFICIAL_SAMPLES_PER_PACKET
DEFAULT_SAMPLE_RATE = OFFICIAL_SAMPLE_RATE

FRAME_TYPE_AUDIO = 0x0001
FRAME_TYPE_RESULT = 0x0002
FRAME_TYPE_STATUS = 0x0003
FRAME_TYPE_ENHANCED_AUDIO = 0x0004
FRAME_TYPE_DEBUG_DUMP = 0x0005
FRAME_TYPE_PARAM_STATUS = 0x0006
FRAME_TYPE_RESULT_V2 = 0x0007
FRAME_TYPE_MULTISOURCE_RESULT = 0x0008
FRAME_TYPE_RTOS_STATUS = 0x0009
FRAME_TYPE_COMMAND_ACK = 0x000A
FRAME_TYPE_WDAS_DIAGNOSTICS = 0x000B
RTOS_SCHEMA_VERSION = 0x00010000
WDAS_QUALITY_VERSION = 0x57514731
WDAS_CALIBRATION_IDENTITY_UNMEASURED = 1
FRAME_TYPE_COMMAND = 0x8001
FRAME_TYPE_PARAM_BLOB = 0x8002

MULTISOURCE_SCHEMA_VERSION = 0x00010000
MULTISOURCE_ALGORITHM_TF_ZONE_V1 = 0x54465A31
MULTISOURCE_MAX_SOURCES = 4

MULTISOURCE_FLAG_VALID = 0x00000001
MULTISOURCE_FLAG_NO_VALID_SOURCE = 0x00000002
MULTISOURCE_FLAG_WARMUP = 0x00000004
MULTISOURCE_FLAG_TRUNCATED = 0x00000008

MULTISOURCE_SOURCE_FLAG_ACCEPTED = 0x0001
MULTISOURCE_SOURCE_FLAG_ANONYMOUS = 0x0002

FRAME_STATUS_FLAG_PDM_RUNNING = 0x00000001
FRAME_STATUS_FLAG_DMA_TIMEOUT = 0x00000002
FRAME_STATUS_FLAG_UDP_ERROR = 0x00000004
FRAME_STATUS_FLAG_PL_OVERFLOW = 0x00000008

FRAME_RESULT_FLAG_VALID = 0x00000001
FRAME_RESULT_FLAG_LOW_CONF = 0x00000002
FRAME_RESULT_FLAG_DISTANCE_VALID = 0x00000004
FRAME_RESULT_FLAG_MARKER_VALID = 0x00000008
FRAME_RESULT_FLAG_PL_NCC_ACCEL = 0x00000010
FRAME_RESULT_FLAG_PL_NCC_FALLBACK = 0x00000020
FRAME_RESULT_FLAG_PL_CA_QW_ACCEL = 0x00000040
FRAME_RESULT_FLAG_PL_CA_QW_FALLBACK = 0x00000080
FRAME_RESULT_FLAG_ENHANCED_AUDIO_VALID = 0x00000100
FRAME_RESULT_FLAG_DEFAULT_SIM_CONFIG = 0x00000200
FRAME_RESULT_FLAG_SELFTEST = 0x00000400

FRAME_CMD_SET_MODE = 0x00000001
FRAME_CMD_GET_STATUS = 0x00000002
FRAME_CMD_SET_PDM_CLK_DIV = 0x00000003
FRAME_CMD_ENABLE_PDM = 0x00000004
FRAME_CMD_CLEAR_COUNTERS = 0x00000005
FRAME_CMD_SET_AUDIO_MODE = 0x00000006
FRAME_CMD_SET_SSL_BACKEND = 0x00000007
FRAME_CMD_LOAD_DEFAULT_SSL_CONFIG = 0x00000008
FRAME_CMD_RUN_CA_QW_SELFTEST = 0x00000009
FRAME_CMD_DUMP_GCC_PHAT = 0x0000000A
FRAME_CMD_DUMP_PAIR_RELIABILITY = 0x0000000B
FRAME_CMD_DUMP_WEIGHTED_SRP = 0x0000000C
FRAME_CMD_DUMP_CA_QW_LOGITS = 0x0000000D
FRAME_CMD_GET_PARAM_STATUS = 0x0000000E
FRAME_CMD_PARAM_UPLOAD_BEGIN = 0x0000000F
FRAME_CMD_PARAM_UPLOAD_COMMIT = 0x00000010

DEBUG_DUMP_GCC_PHAT = 0x1
DEBUG_DUMP_PAIR_RELIABILITY = 0x2
DEBUG_DUMP_WEIGHTED_SRP = 0x4
DEBUG_DUMP_LOGITS = 0x8

MODE_CAPTURE = 0
MODE_LOCALIZE = 1
MODE_SIM_LOCALIZE = 2


@dataclass
class Mic16Header:
    magic: int
    version: int
    frame_type: int
    frame_id: int
    sample_count: int
    channel_count: int
    sample_bits: int
    payload_bytes: int


@dataclass
class Mic16Packet:
    header: Mic16Header
    samples: np.ndarray
    source: Tuple[str, int]


@dataclass
class Mic16Frame:
    header: Mic16Header
    payload: bytes
    source: Tuple[str, int]


@dataclass
class Mic16Status:
    frame_id: int
    run_mode: int
    dma_ok: int
    udp_ok: int
    fail: int
    dma_timeouts: int
    dma_resets: int
    udp_send_failures: int
    last_dma_status: int
    last_udp_status: int
    flags: int


@dataclass(frozen=True)
class Mic16RtosStatus:
    schema_version: int
    sequence: int
    algorithm_version: int
    requested_generation: int
    applied_generation: int
    requested_mode: int
    applied_mode: int
    requested_audio_mode: int
    requested_pdm_enabled: int
    requested_pdm_clk_div: int
    calibration_ready: int
    calibration_frames: int
    calibration_crc32: int
    fault_flags: int
    captured_frames: int
    capture_drops: int
    raw_frames_queued: int
    raw_frame_drops: int
    udp_packets: int
    udp_errors: int
    continuity_breaks: int
    window_discards: int
    dma_reset_failures: int
    pl_errors: int
    protocol_errors: int
    command_rejections: int
    applied_audio_mode: int
    raw_packets_sent: int


@dataclass(frozen=True)
class Mic16CommandAck:
    schema_version: int
    request_id: int
    command_id: int
    result: int
    arg0: int
    requested_generation: int
    applied_generation: int
    fault_flags: int


@dataclass(frozen=True)
class Mic16WdasDiagnostics:
    schema_version: int
    sequence: int
    requested_generation: int
    applied_generation: int
    quality_version: int
    quality_calibration_flags: int
    quality_ready: int
    wdas_ready: int
    quality_frames: int
    quality_errors: int
    quality_fallback_frames: int
    quality_last_valid_pairs: int
    quality_last_active_channels: int
    quality_max_us: int
    audio_frame_max_us: int
    audio_frame_over_budget: int
    audio_frame_budget_us: int
    pl_wait_sleeps: int
    wdas_wait_sleeps: int
    wdas_frames: int
    wdas_errors: int
    enhanced_windows_queued: int
    enhanced_frame_drops: int
    enhanced_no_source_frames: int
    wdas_history_resets: int
    wdas_warmup_samples: int
    wdas_kernel_calls: int
    enhanced_packets_sent: int
    enhanced_packet_send_failures: int
    fault_flags: int
    last_capture_frame_id: int
    uptime_ms: int


def parse_wdas_diagnostics_payload(payload: bytes) -> Mic16WdasDiagnostics:
    if len(payload) != 128:
        raise ValueError("WDAS_DIAGNOSTICS must be exactly 128 bytes")
    d = Mic16WdasDiagnostics(*struct.unpack("<32I", payload))
    if (d.schema_version != RTOS_SCHEMA_VERSION or d.quality_version != WDAS_QUALITY_VERSION
            or d.quality_calibration_flags != WDAS_CALIBRATION_IDENTITY_UNMEASURED
            or d.quality_ready > 1 or d.wdas_ready > 1
            or d.quality_last_valid_pairs > 32 or d.quality_last_active_channels > 16
            or d.audio_frame_budget_us != 52429 or d.fault_flags & ~7):
        raise ValueError("invalid WDAS_DIAGNOSTICS schema/fields")
    # 各累计计数器可能独立回绕，不能由周期快照推断不等式，
    # 也不能虚构精确丢帧ID。
    return d


def parse_rtos_status_payload(payload: bytes) -> Mic16RtosStatus:
    if len(payload) != 112:
        raise ValueError("RTOS_STATUS must be exactly 112 bytes")
    status = Mic16RtosStatus(*struct.unpack("<28I", payload))
    if status.schema_version != RTOS_SCHEMA_VERSION:
        raise ValueError("unsupported RTOS_STATUS schema")
    if (status.requested_mode > 1 or status.applied_mode > 1
            or status.requested_audio_mode > 2 or status.applied_audio_mode > 2
            or status.requested_pdm_enabled > 1 or status.calibration_ready > 1
            or status.calibration_frames > 20 or status.fault_flags & ~7):
        raise ValueError("invalid RTOS_STATUS fields")
    return status


def parse_command_ack_payload(payload: bytes) -> Mic16CommandAck:
    if len(payload) != 32:
        raise ValueError("COMMAND_ACK must be exactly 32 bytes")
    ack = Mic16CommandAck(*struct.unpack("<8I", payload))
    if ack.schema_version != RTOS_SCHEMA_VERSION or ack.result > 6 or ack.fault_flags & ~7:
        raise ValueError("invalid COMMAND_ACK schema/fields")
    return ack


@dataclass
class Mic16Result:
    frame_id: int
    angle_deg_x100: int
    confidence_q15: int
    angle_bin: int
    latency_us: int
    flags: int
    distance_cm: int = 0
    distance_bin: int = 0
    marker_x: int = 0
    marker_y: int = 0
    reserved: int = 0


@dataclass
class Mic16ResultV2:
    frame_id: int
    algorithm_version: int
    audio_mode: int
    backend: int
    angle_deg_x100: int
    confidence_q15: int
    angle_bin: int
    top3_bins: tuple[int, int, int]
    valid_pair_count: int
    top3_logits_q16: tuple[int, int, int]
    confidence_head_q16: int
    entropy_q16: int
    peak_margin_q16: int
    latency_us: int
    latency_cycles: int
    flags: int
    param_version: int
    param_crc32: int
    quality_flag: int


@dataclass(frozen=True)
class Mic16MultisourceSource:
    azimuth_deg_x100: int
    elevation_deg_x100: int
    level_gate_margin_q16: int
    entropy_gate_margin_q16: int
    candidate_rank: int
    flags: int
    reserved: int


@dataclass(frozen=True)
class Mic16MultisourceResult:
    frame_id: int
    algorithm_version: int
    schema_version: int
    calibration_crc32: int
    window_frame_count: int
    history_us: int
    source_count: int
    candidate_count: int
    latency_us: int
    flags: int
    sources: tuple[Mic16MultisourceSource, ...]


@dataclass
class Mic16ParamStatus:
    frame_id: int
    param_version: int
    param_crc32: int
    param_flags: int
    upload_expected_bytes: int
    upload_received_bytes: int
    audio_mode: int
    backend: int
    last_status: int


@dataclass
class Mic16DebugDumpChunk:
    frame_id: int
    algorithm_version: int
    param_version: int
    param_crc32: int
    dump_mask: int
    word_offset: int
    total_words: int
    words: np.ndarray


@dataclass
class Mic16EnhancedAudioChunk:
    frame_id: int
    sample_offset: int
    total_samples: int
    samples: np.ndarray


@dataclass
class Mic16Command:
    command_id: int
    arg0: int = 0
    arg1: int = 0
    arg2: int = 0


def calc_checksum(data: bytes) -> int:
    """Checksum used by current Vitis code: byte sum modulo uint32."""
    return sum(data) & 0xFFFFFFFF


def build_frame(frame_type: int,
                payload: bytes,
                frame_id: int = 0,
                sample_count: int = 0,
                channel_count: int = 0,
                sample_bits: int = 0) -> bytes:
    header = struct.pack(
        "<IHHIIHHI",
        MAGIC,
        1,
        frame_type,
        frame_id & 0xFFFFFFFF,
        sample_count & 0xFFFFFFFF,
        channel_count & 0xFFFF,
        sample_bits & 0xFFFF,
        len(payload),
    )
    checksum = struct.pack("<I", calc_checksum(header + payload))
    return header + payload + checksum


def build_command_frame(command: Mic16Command, frame_id: int = 0) -> bytes:
    payload = struct.pack(
        "<IIII",
        command.command_id & 0xFFFFFFFF,
        command.arg0 & 0xFFFFFFFF,
        command.arg1 & 0xFFFFFFFF,
        command.arg2 & 0xFFFFFFFF,
    )
    return build_frame(FRAME_TYPE_COMMAND, payload, frame_id=frame_id)


def build_param_blob_frame(blob: bytes, chunk_offset: int, chunk_bytes: int = 960,
                           frame_id: int = 0) -> bytes:
    if chunk_offset < 0 or chunk_offset >= len(blob):
        raise ValueError("chunk_offset outside parameter blob")
    data = blob[chunk_offset:chunk_offset + max(1, int(chunk_bytes))]
    crc32 = int.from_bytes(blob[16:20], byteorder="little", signed=False)
    header = struct.pack(
        "<6I",
        int.from_bytes(blob[4:8], byteorder="little", signed=False),
        len(blob),
        chunk_offset,
        len(data),
        crc32,
        0,
    )
    return build_frame(FRAME_TYPE_PARAM_BLOB, header + data, frame_id=frame_id)


def iter_param_blob_frames(blob: bytes, chunk_bytes: int = 960,
                           frame_id: int = 0) -> Iterator[bytes]:
    if len(blob) < 24:
        raise ValueError("parameter blob is shorter than its fixed header")
    for offset in range(0, len(blob), max(1, int(chunk_bytes))):
        yield build_param_blob_frame(blob, offset, chunk_bytes=chunk_bytes,
                                     frame_id=(frame_id + offset // max(1, int(chunk_bytes))))


def expected_datagram_bytes(channels: int = DEFAULT_CHANNELS,
                            samples_per_packet: int = DEFAULT_SAMPLES_PER_PACKET) -> int:
    payload = channels * samples_per_packet * 4
    return HEADER_BYTES + payload + CHECKSUM_BYTES


def parse_mic16_frame(packet: bytes,
                      source: Tuple[str, int] = ("", 0),
                      strict_checksum: bool = True) -> Tuple[Optional[Mic16Frame], Optional[str]]:
    if len(packet) < HEADER_BYTES + CHECKSUM_BYTES:
        return None, f"bad length {len(packet)}, shorter than header+checksum"

    header_bytes = packet[:HEADER_BYTES]
    try:
        fields = struct.unpack("<IHHIIHHI", header_bytes)
    except struct.error as exc:
        return None, f"header unpack failed: {exc}"

    header = Mic16Header(*fields)
    if header.magic != MAGIC:
        return None, f"bad magic 0x{header.magic:08X}"

    expected_len = HEADER_BYTES + header.payload_bytes + CHECKSUM_BYTES
    if len(packet) != expected_len:
        return None, f"bad length {len(packet)}, expected {expected_len} from header"

    payload = packet[HEADER_BYTES:HEADER_BYTES + header.payload_bytes]
    checksum_bytes = packet[HEADER_BYTES + header.payload_bytes:]
    recv_checksum = struct.unpack("<I", checksum_bytes)[0]
    local_checksum = calc_checksum(packet[:HEADER_BYTES + header.payload_bytes])
    if strict_checksum and recv_checksum != local_checksum:
        return None, f"bad checksum got 0x{recv_checksum:08X}, expected 0x{local_checksum:08X}"

    return Mic16Frame(header=header, payload=payload, source=source), None


def parse_status_payload(payload: bytes) -> Mic16Status:
    fields = struct.unpack("<IIIIIIIIIiI", payload)
    return Mic16Status(*fields)


def parse_result_payload(payload: bytes) -> Mic16Result:
    new_fmt = "<IiHHIIIHHHH"
    old_fmt = "<IiHHII"
    if len(payload) >= struct.calcsize(new_fmt):
        fields = struct.unpack(new_fmt, payload[: struct.calcsize(new_fmt)])
        return Mic16Result(*fields)

    fields = struct.unpack(old_fmt, payload[: struct.calcsize(old_fmt)])
    return Mic16Result(*fields)


def parse_result_v2_payload(payload: bytes) -> Mic16ResultV2:
    fmt = "<IIIIiHH3HH6i6I"
    size = struct.calcsize(fmt)
    if len(payload) != size:
        raise ValueError(f"RESULT_V2 payload is {len(payload)} bytes, expected {size}")
    values = struct.unpack(fmt, payload)
    return Mic16ResultV2(
        frame_id=values[0],
        algorithm_version=values[1],
        audio_mode=values[2],
        backend=values[3],
        angle_deg_x100=values[4],
        confidence_q15=values[5],
        angle_bin=values[6],
        top3_bins=(values[7], values[8], values[9]),
        valid_pair_count=values[10],
        top3_logits_q16=(values[11], values[12], values[13]),
        confidence_head_q16=values[14],
        entropy_q16=values[15],
        peak_margin_q16=values[16],
        latency_us=values[17],
        latency_cycles=values[18],
        flags=values[19],
        param_version=values[20],
        param_crc32=values[21],
        quality_flag=values[22],
    )


def parse_multisource_result_payload(payload: bytes) -> Mic16MultisourceResult:
    """Parse the fixed PS/UDP ABI for anonymous accepted 2-D sources."""

    header_fmt = "<10I"
    source_fmt = "<4iHHI"
    header_bytes = struct.calcsize(header_fmt)
    source_bytes = struct.calcsize(source_fmt)
    expected_bytes = header_bytes + MULTISOURCE_MAX_SOURCES * source_bytes
    if len(payload) != expected_bytes:
        raise ValueError(
            f"MULTISOURCE_RESULT payload is {len(payload)} bytes, expected {expected_bytes}"
        )
    header = struct.unpack(header_fmt, payload[:header_bytes])
    if header[2] != MULTISOURCE_SCHEMA_VERSION:
        raise ValueError("unsupported MULTISOURCE_RESULT schema_version")
    source_count = int(header[6])
    candidate_count = int(header[7])
    if source_count > MULTISOURCE_MAX_SOURCES:
        raise ValueError("MULTISOURCE_RESULT source_count exceeds fixed capacity")
    if candidate_count < source_count:
        raise ValueError("MULTISOURCE_RESULT candidate_count is smaller than source_count")
    sources: list[Mic16MultisourceSource] = []
    ranks: set[int] = set()
    for index in range(source_count):
        offset = header_bytes + index * source_bytes
        source = Mic16MultisourceSource(*struct.unpack(source_fmt, payload[offset:offset + source_bytes]))
        if not -18000 <= source.azimuth_deg_x100 <= 18000:
            raise ValueError("MULTISOURCE_RESULT azimuth is out of range")
        if not -9000 <= source.elevation_deg_x100 <= 9000:
            raise ValueError("MULTISOURCE_RESULT elevation is out of range")
        if source.candidate_rank < 1 or source.candidate_rank in ranks:
            raise ValueError("MULTISOURCE_RESULT candidate ranks must be unique and positive")
        ranks.add(source.candidate_rank)
        sources.append(source)
    return Mic16MultisourceResult(
        frame_id=header[0],
        algorithm_version=header[1],
        schema_version=header[2],
        calibration_crc32=header[3],
        window_frame_count=header[4],
        history_us=header[5],
        source_count=source_count,
        candidate_count=candidate_count,
        latency_us=header[8],
        flags=header[9],
        sources=tuple(sources),
    )


def parse_param_status_payload(payload: bytes) -> Mic16ParamStatus:
    fmt = "<9I"
    if len(payload) != struct.calcsize(fmt):
        raise ValueError("invalid PARAM_STATUS payload length")
    return Mic16ParamStatus(*struct.unpack(fmt, payload))


def parse_debug_dump_payload(payload: bytes) -> Mic16DebugDumpChunk:
    fmt = "<8I"
    header_bytes = struct.calcsize(fmt)
    if len(payload) < header_bytes:
        raise ValueError("DEBUG_DUMP payload shorter than its header")
    fields = struct.unpack(fmt, payload[:header_bytes])
    chunk_words = fields[7]
    if not 1 <= fields[6] <= MAX_REASSEMBLY_WORDS or chunk_words == 0:
        raise ValueError("DEBUG_DUMP declared size is empty or exceeds limit")
    if len(payload) != header_bytes + chunk_words * 4:
        raise ValueError("DEBUG_DUMP chunk length does not match chunk_words")
    if fields[5] + chunk_words > fields[6]:
        raise ValueError("DEBUG_DUMP chunk exceeds total_words")
    words = np.frombuffer(payload[header_bytes:], dtype="<u4").copy()
    return Mic16DebugDumpChunk(
        frame_id=fields[0],
        algorithm_version=fields[1],
        param_version=fields[2],
        param_crc32=fields[3],
        dump_mask=fields[4],
        word_offset=fields[5],
        total_words=fields[6],
        words=words,
    )


def parse_enhanced_audio_chunk(frame: Mic16Frame) -> Mic16EnhancedAudioChunk:
    if frame.header.frame_type != FRAME_TYPE_ENHANCED_AUDIO:
        raise ValueError("frame is not ENHANCED_AUDIO")
    if frame.header.channel_count != 1 or frame.header.sample_bits != 32:
        raise ValueError("enhanced audio must be mono int32 PCM")
    header_fmt = "<4I"
    header_bytes = struct.calcsize(header_fmt)
    if len(frame.payload) < header_bytes:
        raise ValueError("enhanced audio payload shorter than chunk header")
    frame_id, sample_offset, total_samples, chunk_samples = struct.unpack(
        header_fmt, frame.payload[:header_bytes]
    )
    expected = header_bytes + chunk_samples * 4
    if len(frame.payload) != expected or frame.header.sample_count != chunk_samples:
        raise ValueError("enhanced audio payload length mismatch")
    if frame_id != frame.header.frame_id:
        raise ValueError("enhanced audio frame_id mismatch")
    if not 1 <= total_samples <= MAX_REASSEMBLY_WORDS:
        raise ValueError("enhanced audio declared frame exceeds 65536-sample limit")
    if chunk_samples == 0 or sample_offset + chunk_samples > total_samples:
        raise ValueError("enhanced audio chunk is outside its frame")
    samples = np.frombuffer(frame.payload[header_bytes:], dtype="<i4").copy()
    return Mic16EnhancedAudioChunk(
        frame_id=frame_id,
        sample_offset=sample_offset,
        total_samples=total_samples,
        samples=samples,
    )


def parse_enhanced_audio_payload(frame: Mic16Frame) -> np.ndarray:
    """Return one chunk's PCM samples; use the assembler for complete frames."""
    return parse_enhanced_audio_chunk(frame).samples


class Mic16DebugDumpAssembler:
    """Reassemble one versioned debug dump without mixing frames/configs."""

    def __init__(self) -> None:
        self._key: tuple[int, int, int, int, int] | None = None
        self._words: np.ndarray | None = None
        self._received: np.ndarray | None = None

    def push(self, chunk: Mic16DebugDumpChunk) -> Optional[np.ndarray]:
        if (not 1 <= chunk.total_words <= MAX_REASSEMBLY_WORDS or chunk.word_offset < 0
                or chunk.words.ndim != 1 or chunk.words.size == 0
                or chunk.word_offset + chunk.words.size > chunk.total_words):
            raise ValueError("invalid or oversized debug dump chunk")
        key = (
            chunk.frame_id,
            chunk.algorithm_version,
            chunk.param_version,
            chunk.param_crc32,
            chunk.dump_mask,
        )
        if self._key == key and self._words is not None and self._words.size != chunk.total_words:
            raise ValueError("DEBUG_DUMP total_words changed within one frame/config")
        if self._key != key or self._words is None:
            self._key = key
            self._words = np.zeros(chunk.total_words, dtype="<u4")
            self._received = np.zeros(chunk.total_words, dtype=np.bool_)
        assert self._received is not None
        end = chunk.word_offset + chunk.words.size
        overlap = self._received[chunk.word_offset:end]
        if np.any(self._words[chunk.word_offset:end][overlap] != chunk.words[overlap]):
            raise ValueError("DEBUG_DUMP overlapping chunks disagree")
        self._words[chunk.word_offset:end] = chunk.words
        self._received[chunk.word_offset:end] = True
        if bool(np.all(self._received)):
            completed = self._words.view("<f4").copy()
            self._key = None
            self._words = None
            self._received = None
            return completed
        return None


class Mic16EnhancedAudioAssembler:
    """Reassemble one mono enhanced frame and reject cross-frame mixing."""

    def __init__(self) -> None:
        self._frame_id: int | None = None
        self._samples: np.ndarray | None = None
        self._received: np.ndarray | None = None

    def push(self, chunk: Mic16EnhancedAudioChunk) -> Optional[np.ndarray]:
        if (not 1 <= chunk.total_samples <= MAX_REASSEMBLY_WORDS or chunk.sample_offset < 0
                or chunk.samples.ndim != 1 or chunk.samples.size == 0
                or chunk.sample_offset + chunk.samples.size > chunk.total_samples):
            raise ValueError("invalid or oversized enhanced audio chunk")
        if (self._frame_id == chunk.frame_id and self._samples is not None
                and self._samples.size != chunk.total_samples):
            raise ValueError("enhanced audio total_samples changed within one frame")
        if (
            self._frame_id != chunk.frame_id
            or self._samples is None
            or self._samples.size != chunk.total_samples
        ):
            self._frame_id = chunk.frame_id
            self._samples = np.zeros(chunk.total_samples, dtype="<i4")
            self._received = np.zeros(chunk.total_samples, dtype=np.bool_)
        assert self._received is not None
        end = chunk.sample_offset + chunk.samples.size
        overlap = self._received[chunk.sample_offset:end]
        if np.any(self._samples[chunk.sample_offset:end][overlap] != chunk.samples[overlap]):
            raise ValueError("enhanced audio overlapping chunks disagree")
        self._samples[chunk.sample_offset:end] = chunk.samples
        self._received[chunk.sample_offset:end] = True
        if bool(np.all(self._received)):
            completed = self._samples.copy()
            self._frame_id = None
            self._samples = None
            self._received = None
            return completed
        return None


def _describe_flags(flags: int, mapping: tuple[tuple[int, str], ...]) -> str:
    names = [name for bit, name in mapping if flags & bit]
    known = 0
    for bit, _name in mapping:
        known |= bit
    unknown = flags & ~known
    if unknown:
        names.append(f"UNKNOWN=0x{unknown:08X}")
    return "|".join(names) if names else "none"


def describe_status_flags(flags: int) -> str:
    return _describe_flags(
        flags,
        (
            (FRAME_STATUS_FLAG_PDM_RUNNING, "PDM_RUNNING"),
            (FRAME_STATUS_FLAG_DMA_TIMEOUT, "DMA_TIMEOUT"),
            (FRAME_STATUS_FLAG_UDP_ERROR, "UDP_ERROR"),
            (FRAME_STATUS_FLAG_PL_OVERFLOW, "PL_OVERFLOW"),
        ),
    )


def describe_result_flags(flags: int) -> str:
    return _describe_flags(
        flags,
        (
            (FRAME_RESULT_FLAG_VALID, "VALID"),
            (FRAME_RESULT_FLAG_LOW_CONF, "LOW_CONF"),
            (FRAME_RESULT_FLAG_DISTANCE_VALID, "DISTANCE_VALID"),
            (FRAME_RESULT_FLAG_MARKER_VALID, "MARKER_VALID"),
            (FRAME_RESULT_FLAG_PL_NCC_ACCEL, "PL_NCC_ACCEL"),
            (FRAME_RESULT_FLAG_PL_NCC_FALLBACK, "PL_NCC_FALLBACK"),
            (FRAME_RESULT_FLAG_PL_CA_QW_ACCEL, "PL_CA_QW_ACCEL"),
            (FRAME_RESULT_FLAG_PL_CA_QW_FALLBACK, "PL_CA_QW_FALLBACK"),
            (FRAME_RESULT_FLAG_ENHANCED_AUDIO_VALID, "ENHANCED_AUDIO_VALID"),
            (FRAME_RESULT_FLAG_DEFAULT_SIM_CONFIG, "DEFAULT_SIM_CONFIG"),
            (FRAME_RESULT_FLAG_SELFTEST, "SELFTEST"),
        ),
    )


def describe_quality_flags(flags: int) -> str:
    return _describe_flags(
        flags,
        (
            (0x00000001, "LOW_CONF"),
            (0x00000002, "SEQUENCE_WARMUP"),
            (0x00000004, "INPUT_FRAME"),
            (0x00000008, "PARAM_INVALID"),
            (0x00000010, "LOW_ENERGY"),
            (0x00000020, "SELFTEST"),
        ),
    )


def parse_mic16_packet(packet: bytes,
                       source: Tuple[str, int] = ("", 0),
                       channels: int = DEFAULT_CHANNELS,
                       samples_per_packet: int = DEFAULT_SAMPLES_PER_PACKET,
                       strict_checksum: bool = True) -> Tuple[Optional[Mic16Packet], Optional[str]]:
    payload_bytes = channels * samples_per_packet * 4
    frame, err = parse_mic16_frame(packet, source=source, strict_checksum=strict_checksum)
    if frame is None:
        return None, err

    header = frame.header
    if header.frame_type != FRAME_TYPE_AUDIO:
        return None, f"non-audio frame_type=0x{header.frame_type:04X}"
    if header.sample_count != samples_per_packet:
        return None, f"bad sample_count {header.sample_count}, expected {samples_per_packet}"
    if header.channel_count != channels:
        return None, f"bad channel_count {header.channel_count}, expected {channels}"
    if header.sample_bits != 32:
        return None, f"bad sample_bits {header.sample_bits}, expected 32"
    if header.payload_bytes != payload_bytes:
        return None, f"bad payload_bytes {header.payload_bytes}, expected {payload_bytes}"

    samples = np.frombuffer(frame.payload, dtype="<i4").reshape(samples_per_packet, channels).copy()
    return Mic16Packet(header=header, samples=samples, source=source), None


def is_non_audio_frame(err: Optional[str]) -> bool:
    return bool(err and err.startswith("non-audio frame_type="))


def frame_lost_count(last_frame: Optional[int], current_frame: int) -> int:
    if last_frame is None:
        return 0
    expected = (last_frame + 1) & 0xFFFFFFFF
    if current_frame == expected:
        return 0
    diff = (current_frame - expected) & 0xFFFFFFFF
    if diff > 1_000_000:
        return 0
    return int(diff)


def remove_dc(x: np.ndarray) -> np.ndarray:
    y = x.astype(np.float32)
    return y - y.mean(axis=0, keepdims=True)


def normalize_to_int16(raw_i32: np.ndarray) -> np.ndarray:
    x = remove_dc(raw_i32)
    max_abs = float(np.max(np.abs(x)))
    if max_abs < 1.0:
        max_abs = 1.0
    y = np.clip(x / max_abs * 32767.0, -32768, 32767)
    return y.astype(np.int16)
