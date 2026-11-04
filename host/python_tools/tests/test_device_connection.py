"""本机UDP和Qt交互检查；测试状态回复不代表真实板卡或声学验收。"""
import os
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
import socket
import struct
import sys
import time
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from PySide6.QtWidgets import QApplication
from PySide6.QtNetwork import QHostAddress
from mic16_qt.services.control_wire import COMMAND, STATUS, UNKNOWN, Capability, DeviceState, Operation, Status, decode_status
from mic16_qt.services.device_control import DeviceControl
from mic16_qt.presentation.compact_window import CompactWindow


def spin(predicate, timeout=3.):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        QApplication.processEvents()
        if predicate():
            return
        time.sleep(.005)
    raise AssertionError('Qt条件未按时满足')


class DeviceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.peer.bind(('127.0.0.1', 0))
        self.peer.settimeout(.5)
        self.control = DeviceControl(timeout_s=.08, retries=1)
        self.control.open('127.0.0.1', '127.0.0.1', self.peer.getsockname()[1])

    def tearDown(self):
        self.control.close()
        self.control.deleteLater()
        self.peer.close()
        QApplication.processEvents()

    def respond(self, state=DeviceState.IDLE, *, caps=15, nonce_delta=0, corrupt=False, boot=27, code=0):
        raw, address = self.peer.recvfrom(128)
        magic, version, operation, request, expected_boot, nonce, flags, checksum = COMMAND.unpack(raw)
        self.assertEqual((magic, version, flags, checksum), (b'M16C', 1, 0, zlib.crc32(raw[:-4])))
        response = STATUS.pack(b'M16R', 1, operation, request, boot, nonce + nonce_delta, state, caps,
                               1 if state != DeviceState.IDLE else 0, code, UNKNOWN, UNKNOWN, 0, 1, 0, 0)
        response = response[:-4] + struct.pack('<I', zlib.crc32(response[:-4]) ^ int(corrupt))
        self.peer.sendto(response, address)
        QApplication.processEvents()
        return operation, response, address

    def test_send_is_not_start_confirmation_and_stop_waits_for_idle(self):
        self.assertFalse(self.control.can_start)
        self.respond()
        spin(lambda:self.control.can_start)
        self.assertTrue(self.control.start())
        self.assertEqual(self.control.status.state, DeviceState.IDLE)
        self.assertFalse(self.control.can_start)
        operation, _, _ = self.respond(DeviceState.RUNNING)
        self.assertEqual(operation, Operation.START)
        spin(lambda:self.control.can_stop)
        self.assertTrue(self.control.stop())
        self.assertEqual(self.control.status.state, DeviceState.RUNNING)
        self.respond(DeviceState.DRAINING)
        spin(lambda:self.control.status.state == DeviceState.DRAINING)
        self.assertFalse(self.control.can_start or self.control.can_stop)
        self.control._send(Operation.STATUS)
        self.respond(DeviceState.IDLE)
        spin(lambda:self.control.can_start)

    def test_timeout_is_visible_and_does_not_claim_board_running(self):
        self.control.service()
        self.control.pending['sent'] -= 1
        self.control.service()
        first, _ = self.peer.recvfrom(128)
        repeated, _ = self.peer.recvfrom(128)
        self.assertEqual(first, repeated)
        self.control.pending['sent'] -= 1
        self.control.service()
        self.assertIn('未响应', self.control.message)
        self.assertIsNone(self.control.status)
        self.assertFalse(self.control.can_start or self.control.can_stop)

    def test_bad_nonce_and_crc_never_enable_start(self):
        self.respond(nonce_delta=1)
        spin(lambda:self.control.rejected == 1)
        self.assertFalse(self.control.can_start)
        self.control.socket.writeDatagram(self.control.pending['data'], QHostAddress('127.0.0.1'), self.peer.getsockname()[1])
        self.respond(corrupt=True)
        spin(lambda:self.control.rejected == 2)
        self.assertIsNone(self.control.status)

    def test_start_can_supersede_status_but_not_an_unconfirmed_control(self):
        self.respond()
        spin(lambda:self.control.can_start)
        self.assertTrue(self.control._send(Operation.STATUS))
        raw, address = self.peer.recvfrom(128)
        status_request = COMMAND.unpack(raw)[3]
        self.assertTrue(self.control.can_start)
        self.assertTrue(self.control.start())
        self.assertGreater(self.control.pending['request'], status_request)
        self.assertFalse(self.control._send(Operation.STOP))
        self.assertFalse(self.control.can_start)
        self.respond(DeviceState.RUNNING)
        spin(lambda:self.control.can_stop)
        self.assertTrue(self.control._send(Operation.STATUS))
        self.peer.recvfrom(128)
        self.assertTrue(self.control.can_stop)
        self.assertTrue(self.control.stop())
        self.respond(DeviceState.DRAINING)
        spin(lambda:self.control.status.state == DeviceState.DRAINING)

    def test_old_duplicate_reply_cannot_acknowledge_start(self):
        _, old, address = self.respond()
        spin(lambda:self.control.can_start)
        self.control.start()
        self.peer.sendto(old, address)
        spin(lambda:self.control.rejected == 1)
        self.assertEqual(self.control.pending['operation'], Operation.START)
        self.assertFalse(self.control.can_stop)

    def test_fault_acknowledges_command_and_names_capture_failure(self):
        self.respond()
        spin(lambda:self.control.can_start)
        self.control.start()
        self.respond(DeviceState.FAULT, code=-64)
        spin(lambda:self.control.status.state == DeviceState.FAULT)
        self.assertIsNone(self.control.pending)
        self.assertFalse(self.control.can_start or self.control.can_stop)
        self.assertIn('采集链中断', self.control.message)

    def test_firmware_without_continuous_capability_cannot_start(self):
        self.respond(caps=int(Capability.VIDEO))
        spin(lambda:self.control.status is not None)
        self.assertFalse(self.control.can_start)
        self.assertIn('不支持持续定位', self.control.message)


class WindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.window = CompactWindow(listen_ip='127.0.0.1')
        self.window._board_ip = '127.0.0.1'
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.bind(('127.0.0.1', 0))
        self.window._source_port = probe.getsockname()[1]
        probe.close()

    def tearDown(self):
        self.window.close()
        spin(lambda:self.window._source_udp_thread is None and self.window.video_panel.receiver is None and self.window._heatmap_receiver is None)

    def test_fault_replaces_stale_start_notice(self):
        w=self.window
        w._device_session=w._source_ready=w._active=True
        w._mode='source_udp'
        c=w._device_control;c.connected=True;c.last_received=time.monotonic()
        c.status=Status(Operation.STATUS,3,27,1,DeviceState.FAULT,13,1,-64,UNKNOWN,UNKNOWN,0,10)
        c.message='板端故障（状态码-64：采集链中断或缓冲不足）'
        w.notice.setText('开始请求已发送，等待板端确认')
        w._device_state_changed()
        self.assertIn('已中止',w.notice.text())
        self.assertIn('-64',w.notice.text())
        self.assertNotIn('等待板端确认',w.notice.text())
        self.assertIn('故障',w.source_badge.text())
        self.assertFalse(w.start_button.isEnabled())

    def test_running_ack_replaces_stale_start_notice(self):
        w=self.window
        w._device_session=w._source_ready=w._active=True
        w._mode='source_udp'
        c=w._device_control;c.connected=True;c.last_received=time.monotonic()
        c.status=Status(Operation.START,2,27,1,DeviceState.RUNNING,13,1,0,UNKNOWN,UNKNOWN,0,10)
        c.message='板端正在运行'
        w.notice.setText('开始请求已发送，等待板端确认')
        w._device_state_changed()
        self.assertNotIn('等待板端确认',w.notice.text())
        self.assertIn('持续处理',w.notice.text())

    def test_one_click_without_camera_opens_audio_and_map_and_reports_no_board(self):
        with patch('mic16_qt.presentation.compact_window.QFileDialog.getExistingDirectory', side_effect=AssertionError('普通连接不得要求保存目录')):
            self.window.connect_button.click()
        spin(lambda:self.window._source_ready)
        self.assertIsNone(self.window.video_panel.video_backend)
        self.assertIn('选择USB相机', self.window.video_panel.state.text())
        self.assertIsNotNone(self.window._heatmap_receiver)
        self.assertIsNone(self.window._source_udp_worker.directory)
        self.assertTrue(self.window.video_panel.overlay_enabled.isChecked())
        self.assertEqual(self.window.connect_button.text(), '断开')
        self.assertFalse(self.window.start_button.isEnabled())
        self.window._device_control.pending['sent'] -= 2
        self.window._device_control.service()
        self.window._device_control.pending['sent'] -= 2
        self.window._device_control.service()
        self.assertIn('未响应', self.window.connection_caption.text())
        self.assertEqual(self.window.source_count.text(), '—')

    def test_occupied_localization_port_reports_error_and_never_enables_start(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
            occupied.bind(('127.0.0.1', self.window._source_port))
            self.window.connect_button.click()
            spin(lambda:bool(self.window._source_error))
            self.assertIn('失败', self.window.notice.text())
            self.assertFalse(self.window.start_button.isEnabled())
            self.assertEqual(self.window.source_count.text(), '—')

    def test_disconnect_releases_its_video_map_and_control(self):
        self.window.connect_button.click()
        spin(lambda:self.window._source_ready)
        self.window.connect_button.click()
        spin(lambda:self.window._source_udp_thread is None and self.window.video_panel.receiver is None and self.window._heatmap_receiver is None)
        self.assertFalse(self.window._device_control.connected)
        self.assertEqual(self.window.source_badge.text(), '未连接')

    def test_other_video_endpoint_is_not_silently_reused(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1', 0))
            video_port = probe.getsockname()[1]
        self.window.video_panel.start_video('127.0.0.1', video_port, '127.0.0.1')
        existing = self.window.video_panel.receiver
        self.assertFalse(self.window.connect_device())
        self.assertIn('不同', self.window.notice.text())
        self.assertIs(existing, self.window.video_panel.receiver)
        self.assertIsNone(self.window._source_udp_thread)
        self.assertFalse(self.window._device_control.connected)

    def test_stop_then_start_reopens_source_session_without_download(self):
        def reply(peer, state, epoch):
            raw, address = peer.recvfrom(128)
            _, _, operation, request, _, nonce, _, _ = COMMAND.unpack(raw)
            response = STATUS.pack(b'M16R', 1, operation, request, 27, nonce, state, 15,
                                   epoch, 0, UNKNOWN, UNKNOWN, 0, 1, 0, 0)
            peer.sendto(response[:-4] + struct.pack('<I', zlib.crc32(response[:-4])), address)
            return operation

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            peer.bind(('127.0.0.1', 5004))
            peer.settimeout(1.)
            self.window.connect_button.click()
            self.assertEqual(reply(peer, DeviceState.IDLE, 0), Operation.HELLO)
            spin(lambda:self.window.start_button.isEnabled() and self.window._source_ready)
            self.window.start_button.click()
            self.assertEqual(reply(peer, DeviceState.RUNNING, 1), Operation.START)
            spin(lambda:self.window.stop_button.isEnabled())
            self.window.stop_button.click()
            self.assertEqual(reply(peer, DeviceState.DRAINING, 1), Operation.STOP)
            spin(lambda:self.window._device_control.status.state == DeviceState.DRAINING)
            # 模拟当前时段收到完整最终空源窗；只测试接收生命周期，不伪造真实检出。
            loc = struct.pack('<4sHHIIQQI', b'M16L', 1, 0, 1, 0, 0, 40960, 0)
            loc = loc[:32] + struct.pack('<I', zlib.crc32(loc[:32]))
            end = (40960*256+624)//625
            audio = struct.pack('<4sHHIIIHHQQIIIHHI', b'M16S', 1, 6, 1, 0, 0, 0, 0, 0, end, 0, end, 16000, 0, 0, 0)
            audio = audio[:56] + struct.pack('<I', zlib.crc32(audio[:56]))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                sender.sendto(loc, ('127.0.0.1', self.window._source_port))
                sender.sendto(audio, ('127.0.0.1', self.window._source_port))
            spin(lambda: not self.window._source_udp_thread.isRunning())
            self.window._refresh()
            self.window._device_control._send(Operation.STATUS)
            reply(peer, DeviceState.IDLE, 1)
            spin(lambda:self.window.start_button.isEnabled())
            previous = self.window._source_udp_worker
            self.window.start_button.click()
            spin(lambda:self.window._device_control.pending is not None and self.window._device_control.pending['operation']==Operation.START)
            self.assertIsNot(previous, self.window._source_udp_worker)
            self.assertEqual(reply(peer, DeviceState.RUNNING, 2), Operation.START)
            spin(lambda:self.window._device_control.status.epoch == 2)
            self.assertTrue(self.window._source_ready)


if __name__ == '__main__':
    unittest.main()
