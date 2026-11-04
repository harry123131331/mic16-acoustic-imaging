"""独立科学计算进程的Qt小型邮箱；Qt进程不导入模型。"""
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid

from PySide6.QtCore import QObject, QTimer, Signal

ROOT = Path(__file__).resolve().parents[3]
FILE_RUNNER = ROOT/'release/pc_incremental_c16_frozen_20261015/software/pc_per_source/run_file_job.py'


class PerSourceJobService(QObject):
    progress_changed = Signal(object)
    state_changed = Signal(str)
    finished = Signal(object)
    LOG_CHARS = 65536

    def __init__(self, parent=None):
        super().__init__(parent)
        self._lock = threading.Lock()
        self._active = False
        self._process = None
        self._done = None
        self._progress = self._stage = None
        self._revision = self._seen = 0
        self._cancel_time = None
        self.log_text = ''
        self._timer = QTimer(self)
        self._timer.setInterval(50)
        self._timer.timeout.connect(self._poll)

    @property
    def busy(self):
        return self._active

    def start(self, request, output_root, *, python=None):
        if self.busy:
            raise ValueError('已有逐源任务在运行')
        python = Path(python or os.environ.get('MIC16_EXPERIMENT_PYTHON', 'D:/vscode/python_class/.venv/Scripts/python.exe'))
        if not python.is_file() or not FILE_RUNNER.is_file():
            raise ValueError('现有算法Python或逐源执行器不存在')
        value = dict(request)
        for key in ('pcm_path', 'quiet_path'):
            value[key] = str(Path(value[key]).resolve(strict=True))
        if value['input_origin'] not in ('FILE_REPLAY','SIMULATED_REPLAY'):
            raise ValueError('需要明确录音回放或仿真回放来源')
        encoding=value.get('input_encoding','normalized_float32_fullscale_1')
        if encoding not in ('normalized_float32_fullscale_1','signed_int32_explicit_contract'):
            raise ValueError('不支持的PCM输入格式')
        if encoding=='signed_int32_explicit_contract':
            value['raw_contract_path']=str(Path(value['raw_contract_path']).resolve(strict=True))
        value.update(schema='mic16.per_source.file_job.v1', input_encoding=encoding,
                     sample_rate_hz=39062.5, start_time_s=0.)
        root = Path(output_root).resolve()
        root.mkdir(parents=True, exist_ok=True)
        job = root/('source_'+datetime.now().strftime('%Y%m%d_%H%M%S_')+uuid.uuid4().hex[:10])
        job.mkdir()
        value['segment_id'] = job.name
        (job/'request.json').write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n', encoding='utf-8')
        self.job_dir = job
        with self._lock:
            self._done = self._progress = self._stage = None
            self._revision = self._seen = 0
            self._cancel_time = None
            self._process = None
            self.log_text = ''
        self._active = True
        self._thread = threading.Thread(target=self._work, args=(python,job), daemon=True)
        self._thread.start()
        self._timer.start()
        self.state_changed.emit('正在后台加载；无需等待界面刷新')
        return job

    def _work(self, python, job):
        code, error = -1, ''
        try:
            process = subprocess.Popen([str(python),'-B','-u','-X','utf8',str(FILE_RUNNER),str(job/'request.json')],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                env={**os.environ, 'PYTHONIOENCODING':'utf-8','PYTHONDONTWRITEBYTECODE':'1'},
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            with self._lock:
                self._process = process
            try:
                for raw in iter(lambda:process.stdout.readline(65537), b''):
                    line = raw.decode('utf-8', errors='replace')
                    with self._lock:
                        self.log_text = (self.log_text+line)[-self.LOG_CHARS:]
                    if len(raw) > 65536:
                        continue
                    try:
                        event = json.loads(line)
                        if event.get('kind') == 'progress' and len(event['current_confirmed']) <= 8:
                            with self._lock:
                                self._progress = event
                                self._revision += 1
                        elif event.get('kind') == 'stage':
                            with self._lock:
                                self._stage = str(event['message'])[:256]
                                self._revision += 1
                    except (ValueError, KeyError, TypeError, AttributeError):
                        pass
                code = process.wait()
            finally:
                process.stdout.close()
        except Exception as exc:
            error = str(exc)
            with self._lock:
                process = self._process
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
        finally:
            with self._lock:
                self._done = (code, error)

    def cancel(self):
        if self.busy and self._cancel_time is None:
            (self.job_dir/'cancel.request').touch(exist_ok=True)
            self._cancel_time = time.monotonic()
            self.state_changed.emit('停止中：完成当前计算后保留不完整结果')

    def _poll(self):
        with self._lock:
            revision, progress, stage, done = self._revision, self._progress, self._stage, self._done
            process = self._process
        if revision != self._seen:
            self._seen = revision
            if stage and self._cancel_time is None:
                self.state_changed.emit(stage)
            if progress is not None:
                self.progress_changed.emit(progress)
        if (self._cancel_time is not None and time.monotonic()-self._cancel_time > 30 and
                process is not None and process.poll() is None):
            process.kill()  # 只结束本任务的子进程，绝不接受未完成输出。
            self.state_changed.emit('后台未及时停止，已结束该子进程；结果不完整')
        if done is not None:
            self._finish(*done)

    def _finish(self, code, error):
        self._timer.stop()
        result = dict(status='FAILED', error=error or '缺少有效完成记录')
        try:
            path = self.job_dir/'receipt.json'
            if path.stat().st_size <= 256*1024:
                candidate = json.loads(path.read_text(encoding='utf-8'))
                if candidate['schema'] == 'mic16.per_source.file_job.result.v1' and candidate['status'] in ('COMPLETED','FAILED','CANCELED'):
                    result = candidate
            if code != 0 and result['status'] == 'COMPLETED':
                result.update(status='FAILED', error='进程异常结束，拒绝标记完成')
            if result['status'] == 'COMPLETED':
                session = json.loads((self.job_dir/'audio/session.json').read_text(encoding='utf-8'))
                if session['status'] != 'COMPLETED' or result['statistics']['unprocessed_accepted_samples'] != 0:
                    result.update(status='FAILED', error='连续文件未完整保存')
        except (OSError, ValueError, KeyError, TypeError) as exc:
            result.update(status='FAILED', error=str(exc))
        result.update(job_directory=str(self.job_dir), process_exit_code=code,
                      audio_path=str(self.job_dir/'audio'), Qt_model_imported=False)
        try:
            (self.job_dir/'transport.log').write_text(self.log_text, encoding='utf-8')
            (self.job_dir/'terminal.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        except OSError as exc:
            result.update(status='FAILED',error='保存任务记录失败：'+str(exc))
        self._active = False
        self.finished.emit(result)
