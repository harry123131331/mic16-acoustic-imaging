"""仅本机：日常预览不落盘、晚接入、持续监听及严格录制边界；不是板卡验收。"""
import os,socket,struct,sys,tempfile,threading,time,unittest,zlib
from pathlib import Path
from unittest.mock import patch
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
from PySide6.QtWidgets import QApplication
from mic16_qt.services.source_network import NetworkPreview,NetworkWaveWriter,SourceNetworkSession,SourceNetworkWorker
from mic16_qt.presentation.compact_window import CompactWindow

def location(n,epoch=9):
    h=struct.pack('<4sHHIIQQI',b'M16L',1,0,epoch,n,n*40960,(n+1)*40960,0)
    return h[:32]+struct.pack('<I',zlib.crc32(h[:32]))

def empty_audio(n,final=False):
    a=(n*40960*256+624)//625;b=((n+1)*40960*256+624)//625
    h=struct.pack('<4sHHIIIHHQQIIIHHI',b'M16S',1,4|int(final)*2,9,n,0,0,0,a,b,0,b-a,16000,0,0,0)
    return h[:56]+struct.pack('<I',zlib.crc32(h[:56]))

class PreviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=QApplication.instance() or QApplication([])

    def test_late_join_does_not_pad_or_create_files(self):
        writer=NetworkPreview();session=SourceNetworkSession(writer);peer=('127.0.0.1',12000)
        self.assertIsNone(session.push(empty_audio(7),peer))
        for n in range(8,40):
            self.assertEqual(session.push(location(n),peer)[0],'directions')
            if n==8:self.assertIsNone(session.push(empty_audio(7),peer))
            self.assertEqual(session.push(empty_audio(n,final=n==39),peer)[0],'audio')
            self.assertEqual(len(session.metadata),0)
        self.assertEqual(writer.windows,32);self.assertEqual(writer.result['joined_at_window'],8)
        self.assertEqual(writer.result['first_audio_sample'],(8*40960*256+624)//625)
        self.assertFalse(writer.result['recording_enabled']);self.assertEqual(writer.result['audio_files_saved'],0)
        self.assertEqual(session.ignored_before_join,2);self.assertEqual(writer.result['status'],'COMPLETED')

    def test_live_join_does_not_allow_later_gap_or_epoch_change(self):
        for bad in (location(6),location(5,epoch=10)):
            session=SourceNetworkSession(NetworkPreview());peer=('127.0.0.1',2)
            session.push(location(4),peer);session.push(empty_audio(4),peer)
            with self.assertRaises(ValueError):session.push(bad,peer)
            self.assertTrue(session.failed)

    def test_recording_still_requires_complete_window_zero(self):
        with tempfile.TemporaryDirectory() as td:
            writer=NetworkWaveWriter(Path(td)/'session');session=SourceNetworkSession(writer)
            with self.assertRaises(ValueError):session.push(location(2),('127.0.0.1',1))
            result=session.finish();self.assertEqual(result['status'],'INCOMPLETE')
            self.assertEqual(list((Path(td)/'session').glob('*.wav')),[])

    def test_nonfinite_audio_is_not_shown(self):
        writer=NetworkPreview();bad=dict(output_start_sample=0,output_end_sample_exclusive=3,output_rate=16000,
            epoch=9,final=False,tracks={1:dict(WDAS=np.array([0.,float('nan'),0.]),enhanced=np.zeros(3))})
        with self.assertRaises(ValueError):writer.audio(bad)
        self.assertEqual(writer.windows,0)

    def test_worker_zero_timeout_can_wait_then_stop_without_recording(self):
        probe=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);probe.bind(('127.0.0.1',0));port=probe.getsockname()[1];probe.close()
        worker=SourceNetworkWorker('127.0.0.1',port,'127.0.0.1',idle_timeout_s=0,evidence_source='LOCAL_UDP_TEST')
        with patch('mic16_qt.services.source_network.NetworkWaveWriter',side_effect=AssertionError('预览不得创建录音')):
            thread=threading.Thread(target=worker.run);thread.start()
            try:
                end=time.monotonic()+3
                while worker.bound_port is None and thread.is_alive() and time.monotonic()<end:time.sleep(.01)
                self.assertEqual(worker.bound_port,port);time.sleep(.25);self.assertTrue(thread.is_alive())
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:sender.sendto(location(14),('127.0.0.1',port))
                end=time.monotonic()+3;latest={}
                while time.monotonic()<end and 'directions' not in latest:
                    latest.update(worker.take_snapshot());time.sleep(.01)
                self.assertEqual(latest['directions']['window'],14)
            finally:worker.stop();thread.join(3)
        self.assertFalse(thread.is_alive());self.assertIsNone(worker.directory)
        self.assertEqual(worker.result['status'],'STOPPED');self.assertFalse(worker.result['recording_enabled'])
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as again:again.bind(('127.0.0.1',port))

    def test_daily_connect_is_manual_and_has_no_folder_prompt(self):
        window=CompactWindow();window._timer.stop()
        try:
            self.assertEqual(window._mode,'idle');self.assertEqual(window.source_combo.currentData(),'source_udp')
            self.assertEqual(window._source_timeout,0)
            with patch('mic16_qt.presentation.compact_window.QFileDialog.getExistingDirectory',side_effect=AssertionError('连接不应强制录制')):
                with patch.object(window,'start_source_udp',return_value=True) as start:
                    window.toggle_connection();start.assert_called_once_with('0.0.0.0',5001,'192.168.1.10')
            self.assertFalse(window.control_button.isEnabled())
            self.assertIn('未接通',window.control_button.text())
        finally:window.close();self.app.processEvents()

if __name__=='__main__':unittest.main()
