"""后台进程传输，使用有界邮箱、独占输出目录，不通过shell执行。匿名管道适用于Qt命名管道受限的Windows/离屏环境；工作线程不访问QWidget。"""
from __future__ import annotations

import codecs
from dataclasses import asdict
from datetime import datetime
import json
import os
from pathlib import Path
import signal
import subprocess
import threading
import uuid

from PySide6.QtCore import QObject, QTimer, Signal
from mic16_qt.services.experiment_contract import ExperimentRequest, TOOL_ROOT, BOUNDARY


class ExperimentService(QObject):
    log_changed = Signal(str)
    state_changed = Signal(str)
    finished = Signal(object)
    LOG_CHARS = 64 * 1024

    def __init__(self, parent=None, timeout_ms=30 * 60 * 1000, child_filename="mic16_qt_experiment_child.py"):
        super().__init__(parent)
        if child_filename not in ("mic16_qt_experiment_child.py", "mic16_qt_frozen_scene_child.py"):
            raise ValueError("未授权的离线执行器")
        self.child_filename = child_filename
        self.process = None
        self.job_dir = None
        self.log_text = ""
        self.log_dropped_chars = 0
        self._active = False
        self._lock = threading.Lock()
        self._thread = None
        self._cancel = self._timeout = False
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.setInterval(timeout_ms)
        self.timer.timeout.connect(self._timed_out)
        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(50)
        self._poll_timer.timeout.connect(self._poll)

    @property
    def busy(self):
        return self._active

    def start(self, request: ExperimentRequest, output_root: Path) -> Path:
        if self.busy:
            raise ValueError("已有任务在运行，请先等待或取消")
        request = request.canonical()
        root = Path(output_root).resolve()
        tool_root = TOOL_ROOT
        if (tool_root.parent.parent / "MANIFEST.sha256").is_file() and root.is_relative_to(tool_root.parent.parent):
            raise ValueError("提交包只读：任务输出请选择包外目录")
        child = tool_root / self.child_filename
        if not child.is_file():
            raise ValueError("缺少受管离线执行器")
        job = root / (datetime.now().strftime("job_%Y%m%d_%H%M%S_") + uuid.uuid4().hex[:12])
        job.mkdir(parents=True, exist_ok=False)
        (job / "request.json").write_text(json.dumps(asdict(request), ensure_ascii=False, indent=2), encoding="utf-8")
        self.job_dir, self.log_text, self.log_dropped_chars = job, "", 0
        self._cancel = self._timeout = False
        self._active = True
        self._mail_text = ""
        self._mail_dropped = 0
        self._mail_stage = ""
        self._mail_done = None
        self._revision = self._seen_revision = 0
        self.state_changed.emit("启动中；进度按真实阶段显示，不估算百分比")
        self._thread = threading.Thread(target=self._work, args=(request.python, child, job, tool_root), daemon=True)
        self._thread.start()
        self._poll_timer.start(); self.timer.start()
        return job

    def _write_mail(self, text):
        with self._lock:
            combined = self._mail_text + text
            dropped = max(0, len(combined) - self.LOG_CHARS)
            self._mail_dropped += dropped
            self._mail_text = combined[dropped:]
            stages = [line for line in self._mail_text.splitlines() if line.startswith("[STAGE] ")]
            if stages:
                self._mail_stage = stages[-1][8:]
            self._revision += 1

    def _work(self, python, child, job, tool_root):
        code, error = -1, ""
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        try:
            process = subprocess.Popen([python, "-u", str(child), str(job / "request.json")], cwd=tool_root,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                env={**os.environ, "PYTHONIOENCODING": "utf-8", "PYTHONDONTWRITEBYTECODE": "1"},
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            with self._lock:
                self.process = process
                if self._cancel:
                    process.kill()
            assert process.stdout is not None
            try:
                for block in iter(lambda: process.stdout.read1(4096), b""):
                    self._write_mail(decoder.decode(block))
                self._write_mail(decoder.decode(b"", final=True))
                code = process.wait()
            finally:
                process.stdout.close()
        except Exception as exc:
            error = str(exc)
            self._write_mail("\n[ERROR] " + error + "\n")
            with self._lock:
                if self.process is not None and self.process.poll() is None:
                    self.process.kill(); self.process.wait()
        finally:
            with self._lock:
                self._mail_done = (code, error)

    def _poll(self):
        with self._lock:
            revision, text, dropped, stage, done = (self._revision, self._mail_text, self._mail_dropped,
                                                  self._mail_stage, self._mail_done)
        if revision != self._seen_revision:
            self._seen_revision = revision
            self.log_text, self.log_dropped_chars = text, dropped
            self.log_changed.emit(text)
            if stage and not self._cancel:
                self.state_changed.emit(stage)
        if done is not None:
            self._done(*done)

    def _timed_out(self):
        self._timeout = True
        self.cancel()

    def cancel(self):
        if not self.busy:
            return
        with self._lock:
            self._cancel = True
            process = self.process
            if process is not None and process.poll() is None:
                if os.name == "nt":
                    process.kill()
                else:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        process.kill()
        self.state_changed.emit("取消中；部分产物保留，但不记为完成")

    def _done(self, code, error):
        if not self.busy:
            return
        self.timer.stop(); self._poll_timer.stop()
        result = {"status": "FAILED", "error_zh": error or "进程未留下有效完成清单", "boundary_zh": BOUNDARY}
        receipt = self.job_dir / "receipt.json"
        try:
            if receipt.stat().st_size <= 4 * 1024 * 1024:
                candidate = json.loads(receipt.read_text(encoding="utf-8"))
                if isinstance(candidate, dict) and candidate.get("schema") == "mic16.qt.offline_job.v1" and candidate.get("status") in ("FAILED", "COMPLETED_TOOL_ONLY", "PARTIAL_TOOL_OUTPUT"):
                    result = candidate
        except (OSError, ValueError):
            pass
        if self._cancel:
            result["status"] = "TIMED_OUT" if self._timeout else "CANCELED"
        elif code != 0:
            result["status"] = "FAILED"
        elif result["status"] in ("COMPLETED_TOOL_ONLY", "PARTIAL_TOOL_OUTPUT") and (not result.get("artifacts_sha256") or result.get("runner_exit_code") != 0):
            result["status"] = "FAILED"
            result["error_zh"] = "无有效产物或旧工具退出码；拒绝伪完成清单"
        result.update(job_dir=str(self.job_dir), process_exit_code=int(code), ui_log_dropped_chars=self.log_dropped_chars)
        try:
            (self.job_dir / "transport.log").write_text(self.log_text, encoding="utf-8")
            (self.job_dir / "terminal.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        except OSError as exc:
            result["status"], result["error_zh"] = "FAILED", "无法保存结束清单：" + str(exc)
        self.process, self._active = None, False
        self.state_changed.emit({"COMPLETED_TOOL_ONLY": "完成（仅工具与产物，不是声学/实板验收）",
            "PARTIAL_TOOL_OUTPUT": "部分完成（核心图缺失，查看清单告警）",
            "CANCELED": "已取消（保留部分产物）", "TIMED_OUT": "超时取消（保留部分产物）"}.get(result["status"], "失败：" + result.get("error_zh", "查看日志")))
        self.finished.emit(result)

    def shutdown(self):
        if self.busy:
            self.cancel()
            self._thread.join(1.5)
            self._poll()
