"""Compact UI contracts and host-loopback wiring; never a board acceptance."""
from __future__ import annotations

import csv
import os
from pathlib import Path
import socket
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
import numpy as np
from PySide6.QtWidgets import QApplication, QStackedWidget
from mic16_qt.app import build_parser
from mic16_qt.core.models import LocalizationUpdate, SourceEstimate
from mic16_qt.presentation.compact_window import CompactWindow
from mic16_qt.services.audio_sources import AudioFrameFactory
from mic16_qt.services.recorder import PcmSessionRecorder
from mic16_udp_common import FRAME_TYPE_AUDIO, build_frame


def spin(predicate, timeout=4.0):
    end = time.monotonic()+timeout
    while time.monotonic()<end:
        QApplication.processEvents()
        if predicate():return
        time.sleep(.005)
    raise AssertionError('Qt condition timed out')


class CompactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        root=TOOLS.parent/'results/test_tmp';root.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(prefix='compact_',dir=root)
        self.root=Path(self.temp.name)
        self.window=CompactWindow()
        self.window._timer.stop()

    def tearDown(self):
        self.window._recorder.stop()
        spin(lambda:not self.window._recorder.busy)
        self.window.close()
        QApplication.processEvents()
        self.temp.cleanup()

    def audio(self, origin='BOARD LIVE', index=0):
        raw=(np.arange(512*16,dtype=np.int32).reshape(512,16)+index*8192)
        return AudioFrameFactory().build(raw,frame_index=index,evidence_source=origin)

    def location(self, sources=True, index=0):
        return LocalizationUpdate(index,(SourceEstimate('S1',-20.,8.,1.),) if sources else (),
                                  1,12.,'HOST TEST / NOT BOARD',0,0)

    def test_default_is_idle_and_has_one_workspace(self):
        self.assertEqual(self.window._mode,'idle')
        self.assertIsNone(self.window._source)
        self.assertIsNone(self.window._udp_worker)
        self.assertIsNone(self.window._last_frame)
        self.assertEqual(self.window.source_count.text(),'—')
        self.assertEqual(len(self.window.findChildren(QStackedWidget)),2)  # 已有方向/视频主标签及波形/频谱辅助标签。
        self.assertEqual(self.window.primary_tabs.count(),2)
        self.assertEqual(self.window.audio_tabs.count(),2)
        self.assertFalse(self.window.record_button.isEnabled())
        self.assertFalse(self.window.control_button.isEnabled())

    def test_demo_requires_explicit_start_and_cannot_be_recorded(self):
        self.window.start_demo();self.window._refresh()
        self.assertIn('非实测',self.window.source_badge.text())
        self.assertEqual(self.window.source_table.rowCount(),2)
        self.assertFalse(self.window.start_recording(self.root))
        self.assertEqual(list(self.root.iterdir()),[])

    def test_audio_does_not_manufacture_localization(self):
        self.window._mode='udp';self.window._active=True
        self.window._apply_audio(self.audio())
        self.assertEqual(self.window.source_table.rowCount(),0)
        self.assertEqual(self.window.source_count.text(),'—')
        self.assertIsNone(self.window._last_localization)
        self.assertFalse(self.window.direction.image.isVisible())

    def test_audio_does_not_erase_an_independent_direction_result(self):
        self.window._mode='udp';self.window._active=True
        self.window._apply_localization(self.location())
        self.window._apply_audio(self.audio())
        self.assertEqual(self.window.source_table.rowCount(),1)
        self.assertEqual(self.window.source_table.item(0,1).text(),'-20.0°')
        self.assertFalse(self.window.direction.image.isVisible())

    def test_empty_result_and_expiry_are_distinct_from_no_result(self):
        self.window._mode='udp';self.window._active=True
        self.window._apply_localization(self.location(False))
        self.assertEqual(self.window.source_count.text(),'0')
        self.window.expire_results(self.window._last_result_received+5.1)
        self.assertEqual(self.window.source_count.text(),'—')
        self.assertIn('过期',self.window.result_state.text())

    def test_disconnect_clears_points_but_preserves_export(self):
        self.window._mode='udp';self.window._active=True
        self.window._apply_localization(self.location())
        self.window.disconnect_source()
        self.assertEqual(self.window.source_table.rowCount(),0)
        target=self.root/'results.csv'
        self.assertTrue(self.window.export_results(target))
        with target.open(encoding='utf-8-sig') as stream:rows=list(csv.DictReader(stream))
        self.assertEqual(rows[0]['azimuth_deg'],'-20.0')
        self.assertEqual(rows[0]['evidence_source'],'BOARD LIVE')
        self.assertEqual(rows[0]['algorithm'],'HOST TEST / NOT BOARD')

    def test_disconnect_removes_demo_status_text(self):
        self.window.start_demo();self.window._refresh()
        self.window.disconnect_source()
        self.assertEqual(self.window.source_badge.text(),'未连接')
        self.assertNotIn('演示空间响应',self.window.map_caption.text())
        self.assertIn('已断开',self.window.notice.text())

    def test_csv_includes_zero_source_result(self):
        self.window._mode='udp';self.window._active=True
        self.window._apply_localization(self.location(False))
        target=self.root/'empty.csv';self.window.export_results(target)
        with target.open(encoding='utf-8-sig') as stream:row=list(csv.DictReader(stream))[0]
        self.assertEqual(row['source_count'],'0')
        self.assertEqual(row['azimuth_deg'],'')

    def test_controls_are_disabled_before_compatible_rtos_status(self):
        self.window._mode='udp';self.window._active=True
        self.assertFalse(self.window.send_board_command(4,1))
        self.assertFalse(self.window.control_button.isEnabled())

    def test_stale_audio_cannot_start_a_new_recording(self):
        self.window._mode='udp';self.window._active=True
        self.window._apply_audio(self.audio())
        self.window._last_audio_received=time.monotonic()-3.0
        self.window._refresh_controls()
        self.assertFalse(self.window.record_button.isEnabled())
        self.assertFalse(self.window.start_recording(self.root))

    def test_selected_channel_keeps_raw_integer_amplitude(self):
        frame=self.audio();self.window._apply_audio(frame)
        self.window.channel_combo.setCurrentIndex(7)
        _,actual=self.window.wave_curve.getData()
        np.testing.assert_array_equal(actual,frame.raw_pcm_i32[:,7])

    def test_replay_is_audio_only_and_recording_is_sample_exact(self):
        first,second=self.audio('REPLAY'),self.audio('REPLAY',1)
        path=self.root/'raw_i32.npy'
        np.save(path,np.concatenate([first.raw_pcm_i32,second.raw_pcm_i32]))
        self.assertTrue(self.window.load_replay(path))
        self.window._apply_audio(first)
        self.assertTrue(self.window.start_recording(self.root,prefix='exact'))
        self.window._apply_audio(first);self.window._apply_audio(second)
        self.assertFalse(self.window.disconnect_source())  # 不允许混合不同录制。
        self.window.stop_recording()
        spin(lambda:not self.window._recorder.busy)
        self.assertIsNotNone(self.window._last_recording)
        np.testing.assert_array_equal(np.load(self.window._last_recording.raw_npy),
                                      np.concatenate([first.raw_pcm_i32,second.raw_pcm_i32]))
        self.assertEqual(self.window.source_table.rowCount(),0)

    def test_close_waits_for_background_save_without_blocking_gui(self):
        self.window.show()
        frame=self.audio('REPLAY');self.window._mode='replay';self.window._active=True
        self.window._apply_audio(frame)
        self.assertTrue(self.window.start_recording(self.root,prefix='closing'))
        self.window._apply_audio(frame)
        original=PcmSessionRecorder.stop
        def slow_stop(recorder,**kwargs):time.sleep(.15);return original(recorder,**kwargs)
        with patch.object(PcmSessionRecorder,'stop',slow_stop):
            begin=time.monotonic();self.window.close()
            self.assertLess(time.monotonic()-begin,.12)
            spin(lambda:not self.window.isVisible())
        self.assertIsNotNone(self.window._last_recording)

    def test_cli_has_idle_default_and_explicit_demo_and_research_flags(self):
        args=build_parser().parse_args([])
        self.assertFalse(args.demo or args.udp or args.research_ui)
        self.assertIsNone(args.replay)

    def test_host_loopback_recording_is_independent_of_display_coalescing(self):
        probe=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        probe.bind(('127.0.0.1',0));port=probe.getsockname()[1];probe.close()
        self.assertTrue(self.window.start_udp('127.0.0.1',port))
        spin(lambda:'监听' in self.window.statusBar().currentMessage())
        expected=[]
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            def packets(start,count):
                for i in range(start,start+count):
                    chunk=(np.arange(256,dtype=np.int32).reshape(16,16)+i*1000)
                    sender.sendto(build_frame(FRAME_TYPE_AUDIO,chunk.astype('<i4').tobytes(),i,16,16,32),('127.0.0.1',port))
                    if start:expected.append(chunk)
            packets(0,32)
            def got_frame():
                self.window._refresh()
                return self.window._last_frame is not None
            spin(got_frame)
            self.assertIsNotNone(self.window._last_frame)
            self.assertTrue(self.window.start_recording(self.root,prefix='host_loopback_not_board'))
            packets(32,96)
            # GUI不轮询显示时，接收及写入线程仍应工作。
            end=time.monotonic()+3
            while self.window._recorder.sample_count<1536 and time.monotonic()<end:time.sleep(.01)
            self.assertEqual(self.window._recorder.sample_count,1536)
            self.window._refresh()
            self.window.stop_recording();spin(lambda:not self.window._recorder.busy)
            np.testing.assert_array_equal(np.load(self.window._last_recording.raw_npy),np.concatenate(expected))
            self.assertEqual(self.window.source_table.rowCount(),0)


if __name__=='__main__':unittest.main()
