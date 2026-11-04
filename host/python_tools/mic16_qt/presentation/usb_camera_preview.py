"""电脑本地UVC预览：仅保留最新一帧，时间戳不冒充板端采样时刻。"""
from __future__ import annotations

from dataclasses import dataclass
import time
from threading import Lock

from PySide6.QtCore import QObject, Slot, Qt
from PySide6.QtGui import QImage, QTransform
from PySide6.QtMultimedia import QVideoFrame, QCamera


def camera_devices():
    """仅在用户打开设置或启动预览时枚举，不在软件启动时激活相机。"""
    from PySide6.QtMultimedia import QMediaDevices
    return list(QMediaDevices.videoInputs())


def device_key(device):
    return bytes(device.id()).hex()


def format_key(fmt):
    size = fmt.resolution()
    return (size.width(), size.height(), fmt.pixelFormat().name,
            float(fmt.minFrameRate()), float(fmt.maxFrameRate()))


def format_label(fmt):
    width, height, pixel, low, high = format_key(fmt)
    return f'{width}×{height} · {low:g}～{high:g}fps · {pixel.removeprefix("Format_")}'


def preferred_format(formats):
    """优先720p约30fps；所列是驱动可用格式，不保证USB线上的编码或实测帧率。"""
    if not formats:
        raise RuntimeError('相机未报告可用视频格式')

    def rank(fmt):
        width, height, pixel, _low, high = format_key(fmt)
        return (int(high >= 25.), int((width, height) == (1280, 720)),
                -abs(width * height - 1280 * 720), int('JPEG' in pixel.upper()), min(high, 30.))

    return max(formats, key=rank)


@dataclass(frozen=True)
class UsbVideoMeta:
    width: int
    height: int
    frame_id: int
    epoch: int
    mount_id: str
    mode_id: tuple
    transform_id: tuple
    flags: int = 0


@dataclass(frozen=True)
class UsbPreviewFrame:
    image: QImage
    meta: UsbVideoMeta
    received_monotonic_s: float
    presentation_time_us: int


class _FrameRelay(QObject):
    """回调只换入一个带锁帧槽；图像转换和绘制留给GUI定时器。"""
    def __init__(self, preview, generation):
        super().__init__(preview)
        self.preview, self.generation = preview, generation

    @Slot(QVideoFrame)
    def frame(self, value):
        self.preview._accept_frame(value, self.generation)

    @Slot(QCamera.Error, str)
    def error(self, _error, message):
        self.preview._accept_error(message, self.generation)


class UsbCameraPreview(QObject):
    def __init__(self, parent=None, *, devices=camera_devices, factories=None, clock=time.monotonic):
        super().__init__(parent)
        self._devices = devices
        self._factories = factories
        self._clock = clock
        self._generation = 0
        self._lock = Lock()
        self._camera = self._session = self._sink = None
        self._relay = None
        self._frame_connected = self._error_connected = False
        self._pending = None
        self._running = False
        self._error = ''
        self._received = self._completed = self._replaced = 0
        self._first_at = self._last_at = None
        self._format = None
        self._key = ''
        self.description = ''

    def start(self, selected_id, selected_format=None):
        if self._running:
            raise RuntimeError('先停止原USB相机会话')
        if not selected_id:
            raise RuntimeError('请在视频设置中选择USB相机；不会自动启用电脑内置相机')
        devices = [item for item in self._devices() if device_key(item) == selected_id]
        if len(devices) != 1:
            raise RuntimeError('所选USB相机未连接或设备标识已改变，请重新选择')
        device = devices[0]
        formats = list(device.videoFormats())
        if selected_format is None:
            fmt = preferred_format(formats)
        else:
            matched = [item for item in formats if format_key(item) == tuple(selected_format)]
            if not matched:
                raise RuntimeError('所选视频格式已不可用，请重新选择')
            fmt = matched[0]
        if self._factories is None:
            from PySide6.QtMultimedia import QCamera, QMediaCaptureSession, QVideoSink
            factories = QCamera, QMediaCaptureSession, QVideoSink
        else:
            factories = self._factories
        self._generation += 1
        generation = self._generation
        self._key = selected_id
        self.description = device.description()
        self._format = format_key(fmt)
        self._received = self._completed = self._replaced = 0
        self._first_at = self._last_at = None
        self._pending = None
        self._error = ''
        try:
            self._camera = factories[0](device, self)
            self._session = factories[1](self)
            self._sink = factories[2](self)
            self._camera.setCameraFormat(fmt)
            self._relay = _FrameRelay(self, generation)
            self._frame_slot = self._relay.frame
            self._error_slot = self._relay.error
            # 不把每一帧排入GUI事件队列，否则忙碌时旧帧会持续堆积。
            self._sink.videoFrameChanged.connect(self._frame_slot, Qt.ConnectionType.DirectConnection)
            self._frame_connected = True
            self._camera.errorOccurred.connect(self._error_slot, Qt.ConnectionType.DirectConnection)
            self._error_connected = True
            self._session.setCamera(self._camera)
            self._session.setVideoSink(self._sink)
            self._running = True
            self._camera.start()
        except Exception:
            self.close()
            raise

    def _accept_error(self, message, generation):
        with self._lock:
            if generation == self._generation and self._running:
                self._error = str(message) or 'USB相机错误'

    def _accept_frame(self, frame, generation):
        with self._lock:
            if not self._running or generation != self._generation or not frame.isValid():
                return
            at = self._clock()
            if self._pending is not None:
                self._replaced += 1
            self._received += 1
            if self._first_at is None:
                self._first_at = at
            self._last_at = at
            # Qt帧显式共享；不在回调里复制720p图像或操作控件。
            self._pending = QVideoFrame(frame), at, self._received

    def take_latest(self):
        with self._lock:
            if not self._running or self._pending is None:
                return None
            frame, received, sequence = self._pending
            self._pending = None
        image = frame.toImage()
        if image.isNull():
            self._error = '相机帧无法转换为预览图像'
            return None
        rotation = int(frame.rotation().value)
        mirrored = bool(frame.mirrored())
        # toImage不应用呈现旋转/镜像；在此完成，几何变化同时使旧叠加绑定失效。
        if rotation:
            image = image.transformed(QTransform().rotate(rotation))
        if mirrored:
            image = image.mirrored(True, False)
        self._completed += 1
        meta = UsbVideoMeta(image.width(), image.height(), sequence, self._generation,
                            self._key, self._format, (rotation, mirrored))
        return UsbPreviewFrame(image.copy(), meta, received, int(frame.startTime()))

    def snapshot(self):
        with self._lock:
            elapsed = 0. if self._first_at is None else self._last_at - self._first_at
            return dict(running=self._running, error=self._error, completed=self._completed,
                        received=self._received, replaced=self._replaced,
                        fps=(self._received - 1) / elapsed if elapsed > 0. else None,
                        selected_driver_format=self._format, source='PC_USB_UVC', physical_sync=False)

    def close(self, timeout=0):
        # generation拒绝旧会话已排队的回调，避免重启后显示停止前画面。
        with self._lock:
            self._running = False
            self._generation += 1
            self._pending = None
        if self._sink is not None and self._frame_connected:
            self._sink.videoFrameChanged.disconnect(self._frame_slot)
        if self._camera is not None and self._error_connected:
            self._camera.errorOccurred.disconnect(self._error_slot)
        if self._camera is not None:
            self._camera.stop()
        if self._session is not None:
            self._session.setVideoSink(None)
            self._session.setCamera(None)
        for obj in (self._sink, self._camera, self._session, self._relay):
            if obj is not None:
                obj.deleteLater()
        self._sink = self._camera = self._session = None
        self._relay = None
        self._frame_connected = self._error_connected = False
        return True
