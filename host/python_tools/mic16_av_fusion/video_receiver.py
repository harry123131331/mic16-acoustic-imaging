"""独立M16V接收线程：两帧组装、一个最新完整帧，不向GUI排队发送像素。"""
from __future__ import annotations

import ipaddress
import socket
import threading
from .video_wire import VideoAssembler


class VideoReceiver:
    def __init__(self, listen_ip='0.0.0.0', port=5002, board_ip='192.168.1.10'):
        self.listen_ip = str(ipaddress.IPv4Address(listen_ip))
        self.board_ip = str(ipaddress.IPv4Address(board_ip))
        if not isinstance(port, int) or isinstance(port, bool) or not 0 <= port <= 65535:
            raise ValueError('视频端口无效')
        self.port = port
        self.bound_port = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._socket = None
        self._thread = None
        self._latest = None
        self._stats = dict(running=False, error='', ignored_peer=0, replaced=0,
                           completed=0, pending_payload_bytes=0, packets=0, rejected=0)

    def start(self):
        """一个实例对应一次会话；重连须创建新实例，避免混用epoch和旧图像。"""
        if self._thread is not None or self._stop.is_set():
            raise RuntimeError('请为新视频会话创建接收器')
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # 不设置SO_REUSEADDR，防止Windows下两个接收器抢同一端口。
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
            sock.bind((self.listen_ip, self.port))
            sock.settimeout(.15)
        except BaseException:
            sock.close()
            raise
        self._socket = sock
        self.bound_port = sock.getsockname()[1]
        self._thread = threading.Thread(target=self._run, name='MIC16-video', daemon=False)
        with self._lock:
            self._stats['running'] = True
        try:
            self._thread.start()
        except BaseException:
            sock.close()
            with self._lock:
                self._stats['running'] = False
            raise
        return self.bound_port

    def _run(self):
        assembler = VideoAssembler()
        try:
            while not self._stop.is_set():
                try:
                    packet, peer = self._socket.recvfrom(2048)
                except socket.timeout:
                    continue
                except OSError:
                    if self._stop.is_set():
                        break
                    raise
                if peer[0] != self.board_ip:
                    with self._lock:
                        self._stats['ignored_peer'] += 1
                    continue
                try:
                    frame = assembler.push(packet, peer)
                except ValueError:
                    frame = None
                with self._lock:
                    self._stats.update(assembler.stats)
                    self._stats['pending_payload_bytes'] = assembler.pending_payload_bytes
                    if frame is not None and not self._stop.is_set():
                        if self._latest is not None:
                            self._stats['replaced'] += 1
                        self._latest = frame
        except Exception as exc:
            with self._lock:
                self._stats['error'] = str(exc)
        finally:
            self._socket.close()
            with self._lock:
                self._stats['running'] = False
                self._stats['pending_payload_bytes'] = 0

    def take_latest(self):
        with self._lock:
            frame, self._latest = self._latest, None
            return frame

    def snapshot(self):
        with self._lock:
            return dict(self._stats, latest_frames=int(self._latest is not None),
                        latest_payload_bytes=len(self._latest.pixels) if self._latest else 0,
                        bound_port=self.bound_port)

    def close(self, timeout=0):
        """GUI用零等待并在后续事件中复查；退出前必须确认线程已结束。"""
        self._stop.set()
        if self._socket is not None:
            self._socket.close()
        if self._thread is not None and self._thread.ident is not None:
            self._thread.join(timeout=max(0, timeout))
        stopped = self._thread is None or not self._thread.is_alive()
        if stopped:
            with self._lock:
                self._latest = None
        return stopped
