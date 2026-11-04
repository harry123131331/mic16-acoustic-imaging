"""中文离线工具，原算法保持不变。"""
from __future__ import annotations

from pathlib import Path
import os
import sys
from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (QCheckBox, QComboBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel,
    QLineEdit, QMessageBox, QPlainTextEdit, QPushButton, QSpinBox, QVBoxLayout, QWidget)
from mic16_qt.services.experiment_contract import ACTIONS, ORIGINS, TOOL_ROOT, ExperimentRequest
from mic16_qt.services.experiment_service import ExperimentService


class OfflineExperimentPage(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.service = ExperimentService(self)
        layout = QVBoxLayout(self)
        note = QLabel("四个旧离线入口已迁移；不启动新HOLD实验、不训练、不调整冻结二维v1。\n"
            "原输入只读，每次新建job目录。SNR须使用相同工况的静音参考；论文入口仍是一维。")
        note.setWordWrap(True)
        layout.addWidget(note)
        form = QFormLayout()
        self.action = QComboBox()
        for key, title in ACTIONS.items():
            self.action.addItem(title, key)
        self.input = QLineEdit(str(TOOL_ROOT.parent / "data/recordings/mic16_10s_raw_i32.npy"))
        self.noise = QLineEdit()
        self.output = QLineEdit(str(TOOL_ROOT.parent.parent / "results/matlab/capture_reports"))
        self.python = QLineEdit(os.environ.get("MIC16_EXPERIMENT_PYTHON", sys.executable))
        self.origin = QComboBox()
        for title, value in zip(("未核实的文件（默认）", "合成输入（人工声明）", "回放文件（人工声明）",
                               "真实录音（未作板端验收）"), ORIGINS):
            self.origin.addItem(title, value)
        self.frames = QSpinBox()
        self.frames.setRange(10, 5000)
        self.frames.setSingleStep(50)
        self.frames.setValue(300)
        self.preprocess = QCheckBox("启用静音参考预处理（默认关闭）")
        self.method = QComboBox()
        self.method.addItems(("shared-wiener", "per-channel-wiener"))
        self.config = QWidget()
        self.config.setLayout(form)
        form.addRow("离线任务", self.action)
        for label, edit, browse in (("原始16ch PCM", self.input, self._browse_input),
            ("静音参考", self.noise, self._browse_noise), ("任务输出根目录", self.output, self._browse_output),
            ("执行Python环境", self.python, self._browse_python)):
            row = QWidget(); row_layout = QHBoxLayout(row)
            row_layout.setContentsMargins(0, 0, 0, 0)
            row_layout.addWidget(edit, 1)
            button = QPushButton("选择…")
            button.clicked.connect(browse)
            row_layout.addWidget(button)
            form.addRow(label, row)
        controls = QWidget(); controls_layout = QHBoxLayout(controls)
        controls_layout.setContentsMargins(0, 0, 0, 0)
        controls_layout.addWidget(QLabel("最大帧数")); controls_layout.addWidget(self.frames)
        controls_layout.addWidget(self.preprocess); controls_layout.addWidget(self.method)
        controls_layout.addStretch()
        form.addRow("旧分析参数", controls)
        form.addRow("输入来源声明", self.origin)
        layout.addWidget(self.config)
        self.state = QLabel("空闲；尚未运行任务")
        self.state.setWordWrap(True)
        layout.addWidget(self.state)
        row = QHBoxLayout()
        self.run_button = QPushButton("开始离线任务")
        self.run_button.setObjectName("PrimaryButton")
        self.cancel_button = QPushButton("取消任务")
        self.open_button = QPushButton("打开本次结果目录")
        self.cancel_button.setEnabled(False); self.open_button.setEnabled(False)
        self.run_button.clicked.connect(self.run)
        self.cancel_button.clicked.connect(self.service.cancel)
        self.open_button.clicked.connect(self.open_output)
        for button in (self.run_button, self.cancel_button, self.open_button):
            row.addWidget(button)
        row.addStretch()
        layout.addLayout(row)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(400)
        self.log.setPlaceholderText("真实阶段/输出/错误；界面尾部日志≤64Ki字符，磁盘runner.log≤2MiB")
        layout.addWidget(self.log, 1)
        self.receipt = QLabel("清单：等待输入检查、源码/输入/产物SHA256；退出码0不是精度通过。")
        self.receipt.setWordWrap(True)
        layout.addWidget(self.receipt)
        self.action.currentIndexChanged.connect(self._action_changed)
        self.service.log_changed.connect(self._log_changed)
        self.service.state_changed.connect(self.state.setText)
        self.service.finished.connect(self._finished)
        self._action_changed()

    def _action_changed(self):
        analysis = self.action.currentData() in ("compare", "paper")
        self.frames.setEnabled(analysis)
        self.preprocess.setEnabled(analysis)
        self.method.setEnabled(analysis)

    def _browse_input(self):
        name, _ = QFileDialog.getOpenFileName(self, "选择16通道int32 PCM", self.input.text(), "PCM (*.npy *.bin)")
        if name:
            self.input.setText(name)

    def _browse_noise(self):
        name, _ = QFileDialog.getOpenFileName(self, "选择静音参考", self.noise.text(), "PCM (*.npy *.bin)")
        if name:
            self.noise.setText(name)
            self.preprocess.setChecked(True)  # 保留旧界面选择语义。

    def _browse_output(self):
        name = QFileDialog.getExistingDirectory(self, "任务输出（自动建立独立job，提交包请选择包外）", self.output.text())
        if name:
            self.output.setText(name)

    def _browse_python(self):
        name, _ = QFileDialog.getOpenFileName(self, "选择已安装的Python环境", self.python.text(), "Python (python.exe python python3)")
        if name:
            self.python.setText(name)

    def run(self):
        try:
            if not self.output.text().strip():
                raise ValueError("请指定输出根目录")
            request = ExperimentRequest(action=self.action.currentData(), input_path=self.input.text().strip(),
                noise_path=self.noise.text().strip(), max_frames=self.frames.value(),
                preprocess_enabled=self.preprocess.isChecked(), preprocess_method=self.method.currentText(),
                origin=self.origin.currentData(), python=self.python.text().strip())
            job = self.service.start(request, Path(self.output.text()))
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, "任务未启动", str(exc))
            return
        self.config.setEnabled(False); self.run_button.setEnabled(False)
        self.cancel_button.setEnabled(True); self.open_button.setEnabled(True)
        self.receipt.setText(f"本次目录：{job}\n完成清单尚未生成；已有正式结果不会被覆盖。")

    def _log_changed(self, text):
        self.log.setPlainText(text)
        self.log.verticalScrollBar().setValue(self.log.verticalScrollBar().maximum())

    def _finished(self, result):
        self.config.setEnabled(True); self._action_changed(); self.run_button.setEnabled(True)
        self.cancel_button.setEnabled(False)
        self.receipt.setText(f"状态：{result['status']} | 已核验产物：{len(result.get('artifacts_sha256', {}))} | "
            f"界面截断：{result['ui_log_dropped_chars']}字符\n"
            f"{result['job_dir']} / terminal.json\n离线工具产物≠冻结二维精度/声学增益/实板验收。")
        for warning in result.get("warnings_zh", ()):
            self.log.appendPlainText("[告警] " + warning)

    def open_output(self):
        if self.service.job_dir is not None:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.service.job_dir)))
