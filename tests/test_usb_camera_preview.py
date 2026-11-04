"""离线UVC生命周期与真实Qt帧呈现检查；不枚举、不打开真实摄像头。"""
from dataclasses import dataclass, replace
from pathlib import Path
import json
import os
import sys
import time
import unittest
from threading import Thread

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host/python_tools'))

from PySide6.QtCore import QObject, Signal, QSize
from PySide6.QtGui import QImage, QColor
from PySide6.QtWidgets import QApplication
from PySide6.QtMultimedia import QVideoFrame, QVideoFrameFormat, QtVideo, QCamera
from mic16_qt.presentation.usb_camera_preview import (
    UsbCameraPreview, preferred_format, device_key, format_key,
)
from mic16_qt.presentation.video_panel import VideoPanel
from mic16_qt.core.models import SourceEstimate


@dataclass
class Format:
    width: int
    height: int
    fps: float
    pixel: object = QVideoFrameFormat.PixelFormat.Format_Jpeg

    def resolution(self): return QSize(self.width, self.height)
    def pixelFormat(self): return self.pixel
    def minFrameRate(self): return self.fps
    def maxFrameRate(self): return self.fps


class Device:
    def id(self): return b'offline-usb-fixture'
    def description(self): return '离线UVC夹具，不是真实相机'
    def videoFormats(self):
        return [Format(1280, 720, 10.), Format(640, 480, 30.), Format(1280, 720, 30.)]


class Camera(QObject):
    errorOccurred = Signal(QCamera.Error, str)

    def __init__(self, device, parent=None):
        super().__init__(parent); self.device = device; self.started = False
    def setCameraFormat(self, fmt): self.fmt = fmt
    def start(self): self.started = True
    def stop(self): self.started = False


class Session(QObject):
    def setCamera(self, camera): self.camera = camera
    def setVideoSink(self, sink): self.sink = sink


class Sink(QObject):
    videoFrameChanged = Signal(QVideoFrame)


def fixture_frame(color='red'):
    image = QImage(64, 36, QImage.Format.Format_RGB32)
    image.fill(QColor(color))
    return QVideoFrame(image)


def emit_frame(backend, frame):
    backend._sink.videoFrameChanged.emit(frame)
    QApplication.processEvents()


class UsbChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def backend(self, **kwargs):
        return UsbCameraPreview(devices=lambda: [Device()], factories=(Camera, Session, Sink), **kwargs)

    def test_explicit_selection_formats_and_no_fallback(self):
        enumerations = []
        backend = UsbCameraPreview(devices=lambda: enumerations.append(True) or [Device()],
                                   factories=(Camera, Session, Sink))
        self.assertEqual(enumerations, [])
        with self.assertRaisesRegex(RuntimeError, '不.*内置相机'):
            backend.start('')
        self.assertEqual(enumerations, [])
        with self.assertRaisesRegex(RuntimeError, '未连接'):
            backend.start('not-the-selected-device')
        with self.assertRaisesRegex(RuntimeError, '不可用'):
            backend.start(device_key(Device()), (1920, 1080, 'invalid', 30., 30.))
        self.assertEqual(format_key(preferred_format(Device().videoFormats()))[:2], (1280, 720))
        self.assertEqual(preferred_format(Device().videoFormats()).fps, 30.)
        backend.close()

    def test_latest_only_and_session_fencing(self):
        now = [10.]
        backend = self.backend(clock=lambda: now[0])
        backend.start(device_key(Device()))
        old_slot = backend._frame_slot
        emit_frame(backend, fixture_frame('red'))
        now[0] = 10.1
        emit_frame(backend, fixture_frame('blue'))
        frame = backend.take_latest()
        self.assertEqual(frame.image.pixelColor(0, 0), QColor('blue'))
        self.assertEqual(frame.meta.frame_id, 2)
        self.assertAlmostEqual(backend.snapshot()['fps'], 10.)
        self.assertEqual(backend.snapshot()['replaced'], 1)
        self.assertIsNone(backend.take_latest())
        self.assertEqual(frame.received_monotonic_s, 10.1)
        self.assertEqual(frame.meta.flags, 0)
        self.assertFalse(backend.snapshot()['physical_sync'])
        backend.close(); old_slot(fixture_frame())
        self.assertIsNone(backend.take_latest())
        backend.start(device_key(Device())); old_slot(fixture_frame())
        self.assertIsNone(backend.take_latest())
        self.assertEqual(backend.snapshot()['received'], 0)
        backend.close()

    def test_rotation_mirror_and_error(self):
        backend = self.backend(); backend.start(device_key(Device()))
        frame = fixture_frame()
        frame.setRotation(QtVideo.Rotation.Clockwise90); frame.setMirrored(True)
        emit_frame(backend, frame)
        presented = backend.take_latest()
        self.assertEqual((presented.meta.width, presented.meta.height), (36, 64))
        self.assertEqual(presented.meta.transform_id, (90, True))
        backend._camera.errorOccurred.emit(QCamera.Error.CameraError, '离线断开夹具')
        QApplication.processEvents()
        self.assertEqual(backend.snapshot()['error'], '离线断开夹具')
        backend.close()

    def test_producer_thread_keeps_one_slot_while_gui_is_not_serviced(self):
        backend = self.backend(); backend.start(device_key(Device()))
        frames = [fixture_frame() for _ in range(100)]
        for index, frame in enumerate(frames): frame.setStartTime(index)
        def produce():
            for frame in frames: backend._sink.videoFrameChanged.emit(frame)
        worker = Thread(target=produce); worker.start(); worker.join(3.)
        self.assertFalse(worker.is_alive())
        self.assertEqual(backend.snapshot()['received'], 100)
        self.assertEqual(backend.snapshot()['replaced'], 99)
        self.assertEqual(backend.take_latest().presentation_time_us, 99)
        self.assertIsNone(backend.take_latest())
        backend.close()

    def test_panel_geometry_staleness_and_independent_audio(self):
        panel = VideoPanel()
        panel._usb_factory = lambda parent: UsbCameraPreview(parent, devices=lambda: [Device()],
                                                            factories=(Camera, Session, Sink))
        self.assertEqual(panel.source_kind, 'usb')
        self.assertIsNone(panel.video_backend)
        panel.usb_device_id = device_key(Device())
        panel.start_selected_video()
        self.assertIsNone(panel.receiver)
        backend = panel.usb_receiver
        emit_frame(backend, fixture_frame())
        panel._poll()
        self.assertEqual((panel.canvas.image.width(), panel.canvas.image.height()), (64, 36))
        self.assertIn('USB视频已到达', panel.state.text())
        panel.overlay_enabled.setChecked(True)
        panel.set_acoustic_sources([SourceEstimate('ID1', 0., 0., .8, 'CURRENT')], window=3)
        self.assertEqual(panel.canvas.markers[0][:2], (.5, .5))
        self.assertIn('未核验同步', panel.canvas.message)
        original = panel.last_frame
        panel.last_frame = replace(original, received_monotonic_s=time.monotonic() - 2.)
        panel._update_overlay(); self.assertFalse(panel.canvas.markers)
        panel.last_frame = original
        rotated = fixture_frame(); rotated.setRotation(QtVideo.Rotation.Clockwise90)
        emit_frame(backend, rotated); panel._poll()
        self.assertFalse(panel.canvas.markers)
        self.assertIn('几何配置改变', panel.canvas.message)
        self.assertTrue(panel.shutdown())
        self.assertIsNone(panel.video_backend)
        self.assertTrue(panel.canvas.image.isNull())
        self.assertFalse(panel.canvas.markers)
        self.assertEqual(len(panel._acoustic_sources), 1)
        panel.close(); self.app.processEvents()

    def test_camera_start_failure_releases_objects(self):
        class FailingCamera(Camera):
            def start(self): raise RuntimeError('启动失败夹具')
        backend = UsbCameraPreview(devices=lambda: [Device()], factories=(FailingCamera, Session, Sink))
        with self.assertRaisesRegex(RuntimeError, '启动失败夹具'):
            backend.start(device_key(Device()))
        self.assertIsNone(backend._camera)
        self.assertIsNone(backend._session)
        self.assertIsNone(backend._sink)
        self.assertFalse(backend.snapshot()['running'])


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(UsbChecks)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    out = ROOT / 'build/verification/usb_preview_20261026/result.json'
    out.write_text(json.dumps(dict(status='PASS' if result.wasSuccessful() else 'FAIL',
        test_methods=result.testsRun, errors=len(result.errors), failures=len(result.failures),
        real_qt_video_frames=True, hardware_camera_opened=False, physical_sync=False,
        board_actions=False), ensure_ascii=False, indent=2), encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
