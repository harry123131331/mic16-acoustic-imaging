"""离线工作流白名单，不修改冻结模型或门限。"""
from __future__ import annotations

from dataclasses import asdict, dataclass
from pathlib import Path
import sys

from mic16_config import OFFICIAL_SAMPLE_RATE

TOOL_ROOT = Path(__file__).resolve().parents[2]
ACTIONS = {
    "split-wavs": "分通道 WAV（独立副本）",
    "snr": "静音参考 SNR 检查",
    "compare": "传统一维算法对比",
    "paper": "传统一维论文图与报告",
}
ORIGINS = ("UNVERIFIED_FILE", "SYNTHETIC", "REPLAY", "REAL_RECORDING_UNVALIDATED")
BOUNDARY = "离线工具执行不等于冻结二维模型、FPGA或实板性能通过；文件来源由用户声明。"


@dataclass(frozen=True)
class ExperimentRequest:
    action: str = "split-wavs"
    input_path: str = ""
    noise_path: str = ""
    max_frames: int = 300
    preprocess_enabled: bool = False
    preprocess_method: str = "shared-wiener"
    origin: str = "UNVERIFIED_FILE"
    python: str = sys.executable

    def validate(self) -> None:
        if self.action not in ACTIONS:
            raise ValueError("只允许已迁移的四个离线入口；不开放训练、调参、采集或HOLD实验")
        if type(self.max_frames) is not int or not 10 <= self.max_frames <= 5000:
            raise ValueError("最大帧数须为10..5000的整数（旧默认300）")
        if type(self.preprocess_enabled) is not bool:
            raise ValueError("预处理开关必须是布尔值")
        if self.preprocess_method not in ("shared-wiener", "per-channel-wiener") or self.origin not in ORIGINS:
            raise ValueError("无效预处理方法或来源声明")
        if not Path(self.python).is_file():
            raise ValueError("Python解释器不存在，请选择具备NumPy/SciPy/Matplotlib的环境")
        required = [self.input_path]
        if self.action == "snr" or (self.action in ("compare", "paper") and self.preprocess_enabled):
            required.append(self.noise_path)
        for name in required:
            path = Path(name).resolve()
            if not name.strip() or not path.is_file() or path.suffix.lower() not in (".npy", ".bin"):
                raise ValueError("请选择存在的16通道raw_i32 .npy/.bin；需要静音参考时不可留空")
            if self.action == "split-wavs" and path.suffix.lower() != ".npy":
                raise ValueError("旧分通道WAV入口仅支持.npy，不能把.bin交给np.load")

    def canonical(self) -> "ExperimentRequest":
        self.validate()
        values = asdict(self)
        for field in ("input_path", "noise_path", "python"):
            values[field] = str(Path(values[field]).resolve()) if values[field].strip() else ""
        return ExperimentRequest(**values)

    def arguments(self, artifacts: Path) -> list[str]:
        """沿用原命令行与默认值，输出始终写入新任务子目录。"""
        self.validate()
        args = [self.action]
        if self.action == "snr":
            args += ["--signal", self.input_path, "--noise", self.noise_path, "--prefix", "mic16"]
        else:
            args += ["--input", self.input_path]
        args += ["--out-dir", str(artifacts), "--sample-rate", str(OFFICIAL_SAMPLE_RATE)]
        if self.action in ("compare", "paper"):
            args += ["--max-frames", str(self.max_frames)]
            if self.preprocess_enabled:
                args += ["--noise-profile", self.noise_path, "--noise-preprocess", self.preprocess_method]
        return args
