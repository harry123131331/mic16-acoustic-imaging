"""Managed old-runner subprocess. No GUI, training, acquisition or new tuning.

Windows: put this supervisor in a kill-on-close job BEFORE spawning anything.
Its descendants inherit the job. Killing the supervisor kills its whole tree.
Normal exit removes that flag only after the old runner has exited.
"""
from __future__ import annotations

import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

from mic16_qt.services.experiment_contract import ExperimentRequest, TOOL_ROOT, BOUNDARY

LOG_LIMIT = 2 * 1024 * 1024
INPUT_LIMIT = 512 * 1024 * 1024


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def stage(text: str) -> None:
    print("[STAGE] " + text, flush=True)


class ProcessTree:
    def __enter__(self):
        self.handle = None
        if os.name != "nt":
            os.setsid()
            return self
        import ctypes
        from ctypes import wintypes as w
        class IO(ctypes.Structure):
            _fields_ = [(n, ctypes.c_ulonglong) for n in ("ReadOperationCount", "WriteOperationCount",
                "OtherOperationCount", "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]
        class Basic(ctypes.Structure):
            _fields_ = [("PerProcessUserTimeLimit", ctypes.c_longlong), ("PerJobUserTimeLimit", ctypes.c_longlong),
                ("LimitFlags", w.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", w.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", w.DWORD), ("SchedulingClass", w.DWORD)]
        class Extended(ctypes.Structure):
            _fields_ = [("BasicLimitInformation", Basic), ("IoInfo", IO),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]
        self.ctypes, self.info = ctypes, Extended()
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateJobObjectW.argtypes = (w.LPVOID, w.LPCWSTR)
        self.kernel.CreateJobObjectW.restype = w.HANDLE
        self.kernel.SetInformationJobObject.argtypes = (w.HANDLE, ctypes.c_int, w.LPVOID, w.DWORD)
        self.kernel.AssignProcessToJobObject.argtypes = (w.HANDLE, w.HANDLE)
        self.kernel.GetCurrentProcess.restype = w.HANDLE
        self.kernel.CloseHandle.argtypes = (w.HANDLE,)
        self.handle = self.kernel.CreateJobObjectW(None, None)
        self.info.BasicLimitInformation.LimitFlags = 0x2000  # 关闭任务对象时结束其子进程。
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        if not self.kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(self.info), ctypes.sizeof(self.info)):
            self.kernel.CloseHandle(self.handle)
            self.handle = None
            raise ctypes.WinError(ctypes.get_last_error())
        if not self.kernel.AssignProcessToJobObject(self.handle, self.kernel.GetCurrentProcess()):
            self.kernel.CloseHandle(self.handle)
            self.handle = None
            raise RuntimeError("不能建立独占进程树；为避免孤儿任务，拒绝启动离线工具")
        return self

    def __exit__(self, *_):
        if self.handle:
            self.info.BasicLimitInformation.LimitFlags = 0
            if not self.kernel.SetInformationJobObject(self.handle, 9, self.ctypes.byref(self.info), self.ctypes.sizeof(self.info)):
                # 任务无法释放时不能报告正常完成。
                raise self.ctypes.WinError(self.ctypes.get_last_error())
            self.kernel.CloseHandle(self.handle)


def validate_pcm(path: Path, minimum: int) -> None:
    import numpy as np
    if not 0 < path.stat().st_size <= INPUT_LIMIT:
        raise ValueError("输入超过512MiB或为空；本入口是有界离线任务，不是全量数据集训练")
    if path.suffix.lower() == ".npy":
        raw = np.load(path, mmap_mode="r", allow_pickle=False)
        if raw.dtype != np.dtype("int32") or raw.ndim != 2 or raw.shape[1] != 16 or raw.shape[0] < minimum:
            raise ValueError(f"需要至少{minimum}点的(N,16) int32原始PCM，不接受归一化浮点/错误通道")
        del raw
    elif path.stat().st_size % 64 or path.stat().st_size // 64 < minimum:
        raise ValueError(".bin须为16ch小端int32交织PCM，长度需是64的倍数且足够长")


def execute(request_file: Path) -> int:
    job = request_file.parent.resolve()
    result = {"schema": "mic16.qt.offline_job.v1", "status": "FAILED", "boundary_zh": BOUNDARY,
              "started_unix": time.time(), "source_sha256": {}, "input_sha256": {}, "artifacts_sha256": {}}
    code = 1
    try:
        request = ExperimentRequest(**json.loads(request_file.read_text(encoding="utf-8"))).canonical()
        result["request"] = json.loads(request_file.read_text(encoding="utf-8"))
        result["request_sha256"] = sha(request_file)
        result["runtime"] = {"python": sys.executable, "version": sys.version}
        if Path(request.python).resolve() != Path(sys.executable).resolve():
            raise ValueError("请求解释器与实际执行解释器不符")
        stage("依赖与输入检查（尚未执行算法）")
        modules = ("numpy",) if request.action == "split-wavs" else ("numpy", "scipy", "matplotlib")
        for module in modules:
            try:
                item = importlib.import_module(module)
            except ImportError as exc:
                raise RuntimeError(f"缺少离线依赖{module}；请在界面选择科研Python环境，不需要重装Qt") from exc
            result["runtime"][module] = getattr(item, "__version__", "unknown")
        inputs = [Path(request.input_path)]
        if request.action == "snr" or (request.action in ("compare", "paper") and request.preprocess_enabled):
            inputs.append(Path(request.noise_path))
        for path in inputs:
            validate_pcm(path, 32 if request.action == "snr" else (2048 if request.action in ("compare", "paper") else 1))
        sidecars = []
        warnings = ["没有已核验真值标签时，不得将输出解释为定位精度。",
            "SNR基于同工况静音参考的功率估计；不代表增强增益或实板噪声指标。"]
        for path in inputs:
            metadata = path.with_name(path.stem.removesuffix("_raw_i32") + "_metadata.json")
            if not metadata.is_file():
                warnings.append(f"未提供录音元数据：{path.name}；采样率/连续性未独立证实，使用旧正式采样率。")
                continue
            if metadata.stat().st_size > 1024 * 1024:
                raise ValueError("录音元数据超过1MiB，拒绝解析")
            data = json.loads(metadata.read_text(encoding="utf-8-sig"))
            if not isinstance(data, dict):
                raise ValueError("录音元数据必须是JSON对象")
            rate = data.get("sample_rate_hz")
            if rate is not None and (type(rate) not in (int, float) or not math.isfinite(rate) or rate != request_rate()):
                raise ValueError("录音采样率与正式常量不符；不可静默用错采样率")
            if data.get("schema_version") == "mic16.qt.recording.v1":
                outcome = data.get("recording_outcome")
                if outcome is not None and (not isinstance(outcome, dict) or outcome.get("status") != "COMPLETED"):
                    raise ValueError("录音任务未完整完成；请检查录制错误，不可将恢复数据作为完整分析输入")
                for field in ("continuous_segment_count", "lost_packets_observed"):
                    if type(data.get(field)) is not int or data[field] < 0:
                        raise ValueError("录音连续性字段无效：" + field)
                if data["continuous_segment_count"] != 1 or data["lost_packets_observed"]:
                    if request.action != "split-wavs":
                        raise ValueError("录音存在已知缺口；旧离线分析不支持跨缺口，请先导出独立连续片段")
                    warnings.append("WAV复制包含已有录音缺口，不具有原始连续采集时间轴。")
            sidecars.append(metadata)
        result["warnings_zh"] = warnings
        # 哈希计算放在工作线程，不阻塞GUI；同时记录原可导入工具。
        for path in sorted([*TOOL_ROOT.glob("mic16*.py"), * (TOOL_ROOT / "mic16_qt").rglob("*.py")]):
            result["source_sha256"][str(path)] = sha(path)
        result["source_sha256"][str(Path(__file__).resolve())] = sha(Path(__file__).resolve())
        for path in inputs:
            result["input_sha256"][str(path)] = sha(path)
        for path in sidecars:
            result["input_sha256"][str(path)] = sha(path)
        if request.action == "paper":
            from mic16_paper_experiment import default_comparison_csv, default_labels_for
            result["warnings_zh"].append("旧论文入口是传统一维分析；可能自动引用已有NN比较表，不能作为冻结二维精度。")
            for extra in (default_comparison_csv(), default_labels_for(Path(request.input_path))):
                if extra is not None:
                    result["input_sha256"][str(extra.resolve())] = sha(extra)
        artifacts = job / "artifacts"
        artifacts.mkdir(exist_ok=False)
        cmd = [sys.executable, "-u", str(TOOL_ROOT / "mic16_experiment_runner.py"), *request.arguments(artifacts)]
        result["command"] = cmd
        stage("运行旧离线入口：" + request.action + "（无调参/无新二维实验）")
        # 继承监督进程的Windows任务对象或POSIX进程组。
        with (job / "runner.log").open("xb") as log:
            process = subprocess.Popen(cmd, cwd=TOOL_ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                env={**os.environ, "PYTHONIOENCODING": "utf-8", "PYTHONDONTWRITEBYTECODE": "1", "MPLBACKEND": "Agg"})
            written = dropped = 0
            assert process.stdout is not None
            try:
                for block in iter(lambda: process.stdout.read1(4096), b""):
                    keep = min(len(block), max(0, LOG_LIMIT - written))
                    log.write(block[:keep]); written += keep; dropped += len(block) - keep
                    sys.stdout.buffer.write(block); sys.stdout.buffer.flush()
                code = process.wait()
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
            result["log_retained_bytes"], result["log_dropped_bytes"] = written, dropped
        result["runner_exit_code"] = code
        if code:
            raise RuntimeError(f"旧工具退出码{code}；详情见runner.log（可能截断）")
        stage("核对输入未变与产物哈希（不验证声学结论）")
        for path, digest in result["input_sha256"].items():
            if sha(Path(path)) != digest:
                raise RuntimeError("执行期间输入变化，结果不得作为有效证据：" + path)
        for path, digest in result["source_sha256"].items():
            if sha(Path(path)) != digest:
                raise RuntimeError("执行期间源码变化，拒绝完成状态：" + path)
        files = sorted(p for p in artifacts.rglob("*") if p.is_file())
        if not files or len(files) > 5000 or sum(p.stat().st_size for p in files) > 2 * 1024**3:
            raise RuntimeError("产物为空或超过任务上限（5000文件/2GiB）；保留部分产物，不宣称完成")
        for path in files:
            if not path.resolve().is_relative_to(artifacts.resolve()) or path.is_symlink():
                raise RuntimeError("产物出现越界链接")
            result["artifacts_sha256"][path.relative_to(job).as_posix()] = sha(path)
        if request.action == "split-wavs" and len(list(artifacts.glob("*.wav"))) != 16:
            raise RuntimeError("未生成全部16通道WAV")
        if request.action == "snr" and not (artifacts / "mic16_snr.csv").is_file():
            raise RuntimeError("缺少SNR表")
        if request.action == "compare" and not (artifacts / "traditional_ssl_compare.csv").is_file():
            raise RuntimeError("缺少传统一维结果表")
        if request.action == "paper" and not (artifacts / "paper_experiment_manifest.json").is_file():
            raise RuntimeError("缺少论文入口产物清单")
        result["status"] = "COMPLETED_TOOL_ONLY"
        if request.action == "paper":
            missing = [name for name in ("paper_spatial_spectrum_line.png", "paper_spatial_spectrum_polar.png",
                "paper_pub_spatial_spectrum_db.png", "paper_pub_spatial_spectrum_surface.png", "paper_channel_health.png")
                if not (artifacts / "paper_figures" / name).is_file()]
            if missing:
                result["status"] = "PARTIAL_TOOL_OUTPUT"
                result["warnings_zh"].append("旧入口退出0但部分核心图未生成：" + ", ".join(missing))
        code = 0
    except Exception as exc:
        result["error_zh"] = str(exc)
        stage("失败：" + str(exc))
        code = code or 1
    finally:
        result["finished_unix"] = time.time()
        result["supervisor_exit_code"] = code
        temporary = job / "receipt.partial.json"
        temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        temporary.replace(job / "receipt.json")
    return code


def request_rate():
    from mic16_config import OFFICIAL_SAMPLE_RATE
    return OFFICIAL_SAMPLE_RATE


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("需要本次任务request.json")
    with ProcessTree():
        raise SystemExit(execute(Path(sys.argv[1]).resolve()))
