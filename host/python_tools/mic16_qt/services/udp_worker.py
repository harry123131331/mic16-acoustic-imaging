from __future__ import annotations

import socket
import threading
import queue
import ipaddress
from datetime import datetime, timezone
import time
import uuid
from collections import deque

from PySide6.QtCore import QObject, Signal, Slot

from mic16_qt.core.models import AcousticFrame, EnhancedAudioUpdate, LocalizationUpdate
from mic16_qt.services.board_protocol import BoardProtocolAccumulator, WdasDiagnosticsReceipt
from mic16_udp_common import Mic16Status, Mic16RtosStatus, Mic16CommandAck, Mic16Command, Mic16WdasDiagnostics, build_command_frame


class UdpReceiverWorker(QObject):
    """阻塞式UDP接收循环，与Qt界面线程隔离。"""

    frame_ready = Signal(object)
    localization_ready = Signal(object)
    enhanced_ready = Signal(object)
    status_ready = Signal(object)
    command_ack_ready = Signal(object)
    diagnostics_ready = Signal(object)
    state_changed = Signal(str)
    receive_error = Signal(str)
    finished = Signal()

    def __init__(self, listen_ip: str, port: int, frame_samples: int = 512, *, coalesce_display=False) -> None:
        super().__init__()
        self.listen_ip = str(listen_ip)
        self.port = int(port)
        self._stop_event = threading.Event()
        self._accumulator = BoardProtocolAccumulator(frame_samples=frame_samples)
        self._listener_session_id = uuid.uuid4().hex
        self._commands: queue.Queue[tuple[bytes, tuple[str, int]]] = queue.Queue(maxsize=16)
        self.coalesce_display = bool(coalesce_display)
        self._display_lock = threading.Lock()
        self._latest = {}
        self._acks = deque(maxlen=16)
        self._display_counts = {"received_pcm_frames": 0, "display_replaced_pcm_frames": 0, "ack_display_overflow": 0}
        self._raw_sink = self._diagnostic_sink = None
        # 16通道帧由32个小UDP包突发到达。
        # GUI或GIL调度暂停时，系统默认接收缓冲区偏小。
        # 传输缓冲区仍有界，不能借此掩盖丢包。
        self.receive_buffer_requested_bytes = 4 * 1024 * 1024
        self.receive_buffer_actual_bytes = None

    def set_recording_sinks(self, raw_sink=None, diagnostic_sink=None):
        """回调只将不可变数据放入队列，禁止访问控件或进行文件IO。"""
        with self._display_lock:
            self._raw_sink, self._diagnostic_sink = raw_sink, diagnostic_sink

    def take_display_snapshot(self):
        with self._display_lock:
            events = [*self._latest.values(), *self._acks]
            counts = dict(self._display_counts)
            self._latest.clear(); self._acks.clear()
        return events, counts

    def _dispatch(self, event):
        with self._display_lock:
            sink = self._raw_sink if isinstance(event, AcousticFrame) else (
                self._diagnostic_sink if isinstance(event, WdasDiagnosticsReceipt) else None)
        if sink is not None:
            sink(event)
        if self.coalesce_display:
            with self._display_lock:
                if isinstance(event, AcousticFrame):
                    self._display_counts["received_pcm_frames"] += 1
                    self._display_counts["display_replaced_pcm_frames"] += int(type(event) in self._latest)
                if isinstance(event, Mic16CommandAck):
                    self._display_counts["ack_display_overflow"] += int(len(self._acks) == self._acks.maxlen)
                    self._acks.append(event)
                else:
                    self._latest[type(event)] = event
            return
        if isinstance(event, AcousticFrame): self.frame_ready.emit(event)
        elif isinstance(event, LocalizationUpdate): self.localization_ready.emit(event)
        elif isinstance(event, EnhancedAudioUpdate): self.enhanced_ready.emit(event)
        elif isinstance(event, (Mic16Status, Mic16RtosStatus)): self.status_ready.emit(event)
        elif isinstance(event, Mic16CommandAck): self.command_ack_ready.emit(event)
        elif isinstance(event, WdasDiagnosticsReceipt): self.diagnostics_ready.emit(event)

    def send_command(self, board_ip: str, port: int, command_id: int, arg0: int = 0) -> bool:
        """由GUI线程调用，只有run()拥有已绑定UDP套接字。旧版ID为0以避免虚构会话/重试保证；不自动重试，收到回执不代表物理应用成功。"""
        address = ipaddress.ip_address(board_ip)
        if address.version != 4 or not 1 <= int(port) <= 65535:
            raise ValueError("需要有效的 IPv4 地址和 UDP 端口")
        try:
            self._commands.put_nowait((build_command_frame(Mic16Command(command_id, arg0, 0, 0), frame_id=0),
                                       (str(address), int(port))))
        except queue.Full:
            return False
        return True

    @Slot()
    def run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(0.20)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, self.receive_buffer_requested_bytes)
            self.receive_buffer_actual_bytes = sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
            sock.bind((self.listen_ip, self.port))
            self.state_changed.emit(f"监听 {self.listen_ip}:{self.port}")
            while not self._stop_event.is_set():
                for _ in range(4):
                    try:
                        command, destination = self._commands.get_nowait()
                    except queue.Empty:
                        break
                    try:
                        sock.sendto(command, destination)
                    except OSError as exc:
                        self.receive_error.emit(f"控制命令发送失败：{exc}")
                try:
                    datagram, source = sock.recvfrom(65535)
                except socket.timeout:
                    continue
                except OSError as exc:
                    if not self._stop_event.is_set():
                        self.receive_error.emit(str(exc))
                    break
                for event in self._accumulator.push_datagram(datagram, source):
                    if isinstance(event, Mic16WdasDiagnostics):
                        event = WdasDiagnosticsReceipt(
                            event, source, datetime.now(timezone.utc).isoformat(timespec="microseconds"),
                            time.monotonic_ns(), self._listener_session_id)
                    self._dispatch(event)
        except OSError as exc:
            self.receive_error.emit(str(exc))
        finally:
            sock.close()
            self.finished.emit()

    def stop(self) -> None:
        self._stop_event.set()
