"""Qt transport/closing tests. Fixture outputs are not acoustic evidence."""
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
import numpy as np
from PySide6.QtWidgets import QApplication
from mic16_qt.services.per_source_job_service import PerSourceJobService
from mic16_qt.presentation.compact_window import CompactWindow


def spin(predicate,timeout=10):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        QApplication.processEvents()
        if predicate():return
        time.sleep(.005)
    raise AssertionError('Qt condition timed out')


class JobTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=QApplication.instance() or QApplication([])

    def setUp(self):
        base=TOOLS.parent/'results/test_tmp';base.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(prefix='source_job_',dir=base)
        self.root=Path(self.temp.name)
        for name in ('pcm.npy','quiet.npy'):(self.root/name).touch()
        self.request=dict(pcm_path=str(self.root/'pcm.npy'),quiet_path=str(self.root/'quiet.npy'),
            pose_key='fixture_success',input_origin='SIMULATED_REPLAY')

    def tearDown(self):
        self.temp.cleanup()

    def test_background_child_finishes_even_when_GUI_does_not_poll(self):
        service=PerSourceJobService()
        progress=[];results=[]
        service.progress_changed.connect(progress.append);service.finished.connect(results.append)
        with patch('mic16_qt.services.per_source_job_service.FILE_RUNNER',TOOLS/'tests/fixtures/per_source_job_child.py'):
            job=service.start(self.request,self.root/'jobs',python=sys.executable)
            try:
                # 故意不处理Qt事件，子进程及管道读取器仍继续运行。
                end=time.monotonic()+10
                while not (job/'receipt.json').exists() and time.monotonic()<end:time.sleep(.01)
                self.assertTrue((job/'receipt.json').exists())
                self.assertEqual(len((job/'all_cycles_fixture.jsonl').read_text().splitlines()),1000)
                self.assertEqual(progress,[])
                spin(lambda:not service.busy)
                self.assertEqual(results[0]['status'],'COMPLETED')
                self.assertLessEqual(len(progress),3)
                self.assertEqual(progress[-1]['cycle'],1000)
                self.assertLessEqual(len(service.log_text),65536)
                self.assertNotIn('torch',sys.modules)
            finally:
                service.cancel();spin(lambda:not service.busy)

    def test_cooperative_cancel_preserves_failure_status(self):
        service=PerSourceJobService();results=[];service.finished.connect(results.append)
        self.request['pose_key']='fixture_cancel'
        with patch('mic16_qt.services.per_source_job_service.FILE_RUNNER',TOOLS/'tests/fixtures/per_source_job_child.py'):
            job=service.start(self.request,self.root/'jobs',python=sys.executable)
            service.cancel();spin(lambda:not service.busy)
            self.assertTrue((job/'cancel.request').is_file())
            self.assertEqual(results[0]['status'],'CANCELED')

    def test_main_close_waits_asynchronously_for_processing_child(self):
        window=CompactWindow();window._timer.stop();window.show()
        dialog=window.open_source_processing()
        self.request['pose_key']='fixture_cancel'
        with patch('mic16_qt.services.per_source_job_service.FILE_RUNNER',TOOLS/'tests/fixtures/per_source_job_child.py'):
            dialog.service.start(self.request,self.root/'jobs',python=sys.executable)
            begin=time.monotonic();window.close()
            self.assertLess(time.monotonic()-begin,.12)
            spin(lambda:not window.isVisible())
        self.assertFalse(dialog.service.busy)

    def test_real_runner_refuses_integer_PCM_before_loading_model(self):
        np.save(self.root/'pcm.npy',np.zeros((40960,16),np.int32))
        np.save(self.root/'quiet.npy',np.zeros((8192,16),np.float32))
        service=PerSourceJobService();results=[];service.finished.connect(results.append)
        job=service.start(self.request,self.root/'jobs',python=sys.executable)
        spin(lambda:not service.busy,timeout=20)
        self.assertEqual(results[0]['status'],'FAILED')
        self.assertFalse((job/'audio').exists())
        self.assertIn('float32',results[0]['error'])


if __name__=='__main__':unittest.main(verbosity=2)
