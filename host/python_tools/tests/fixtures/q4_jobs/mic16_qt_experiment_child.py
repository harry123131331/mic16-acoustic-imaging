"""Process lifecycle fixture ONLY. Does not simulate acoustics or FPGA."""
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from mic16_qt_experiment_child import ProcessTree

request = Path(sys.argv[1])
mode = Path(json.loads(request.read_text(encoding="utf-8"))["input_path"]).stem
with ProcessTree():
    if mode == "hang":
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        (request.parent / "child_pid.json").write_text(json.dumps(child.pid), encoding="utf-8")
        print("[STAGE] 子任务已启动", flush=True)
        time.sleep(60)
    elif mode == "flood":
        sys.stdout.buffer.write(("中文日志" * 40000).encode("utf-8")); sys.stdout.flush()
    elif mode == "malformed":
        (request.parent / "receipt.json").write_text("[]", encoding="utf-8")
    elif mode == "false_completed":
        (request.parent / "receipt.json").write_text(json.dumps({"schema": "mic16.qt.offline_job.v1",
            "status": "COMPLETED_TOOL_ONLY", "runner_exit_code": 0}), encoding="utf-8")
    else:
        print("[STAGE] 故障注入：非零退出", flush=True)
        raise SystemExit(9)
