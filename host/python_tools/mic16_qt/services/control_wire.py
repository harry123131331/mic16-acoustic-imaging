"""常驻新架构控制协议；与旧RTOS命令、定位和音频数据分端口。"""
from dataclasses import dataclass
from enum import IntEnum, IntFlag
import struct
import zlib

PORT = 5004
UNKNOWN = (1 << 64) - 1
COMMAND = struct.Struct('<4sHHIIQII')
STATUS = struct.Struct('<4sHHIIQIIIiQQQQII')


class Operation(IntEnum):
    HELLO = 1
    STATUS = 2
    START = 3
    STOP = 4


class DeviceState(IntEnum):
    IDLE = 0
    STARTING = 1
    RUNNING = 2
    DRAINING = 3
    FAULT = 4


class Capability(IntFlag):
    CONTINUOUS = 1
    VIDEO = 2
    SOURCE_AUDIO = 4
    TELEMETRY = 8
    LOCALIZATION = 16
    RAW_PCM = 32
    SINGLE_CAPTURE = 64
    FAST_DISPLAY = 128


@dataclass(frozen=True)
class Status:
    operation: Operation
    request: int
    boot: int
    nonce: int
    state: DeviceState
    capabilities: int
    epoch: int
    code: int
    window: int
    compute_us: int
    queued_windows: int
    uptime_ms: int
    flags: int = 0


def command(operation, request, boot, nonce):
    operation = Operation(operation)
    if not 0 < request <= 0xffffffff or not 0 <= boot <= 0xffffffff or not 0 < nonce <= UNKNOWN:
        raise ValueError('控制命令编号、启动编号或随机标识无效')
    raw = COMMAND.pack(b'M16C', 1, operation, request, boot, nonce, 0, 0)
    return raw[:-4] + struct.pack('<I', zlib.crc32(raw[:-4]))


def decode_status(raw):
    if len(raw) != STATUS.size:
        raise ValueError('控制状态长度错误')
    magic, version, operation, request, boot, nonce, state, caps, epoch, code, window, compute, queued, uptime, flags, checksum = STATUS.unpack(raw)
    if magic != b'M16R' or version != 1 or checksum != zlib.crc32(raw[:-4]):
        raise ValueError('控制状态版本或校验错误')
    if not request or not boot or not nonce or caps & ~255 or flags:
        raise ValueError('控制状态标识或保留位错误')
    operation, state = Operation(operation), DeviceState(state)
    if state in (DeviceState.STARTING, DeviceState.RUNNING, DeviceState.DRAINING) and not epoch:
        raise ValueError('运行状态缺少时段编号')
    return Status(operation, request, boot, nonce, state, caps, epoch, code, window, compute, queued, uptime, flags)
