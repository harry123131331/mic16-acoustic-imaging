"""独立的冻结场景回放界面，不能替代实板结果或现场会话。"""
import json
import os
from pathlib import Path
import sys

from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QDesktopServices, QPixmap
from PySide6.QtWidgets import (QComboBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel, QLineEdit,
    QMessageBox, QPlainTextEdit, QPushButton, QSplitter, QVBoxLayout, QWidget)

from mic16_qt.services.experiment_contract import TOOL_ROOT
from mic16_qt.services.experiment_service import ExperimentService
from mic16_qt.services.frozen_scene import BOUNDARY, FrozenSceneRequest, catalog, sha


class EvidenceImage(QLabel):
    def __init__(self):
        super().__init__("尚未运行冻结场景复现")
        self.original = None
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMinimumSize(400, 220)

    def set_image(self, path):
        pixmap = QPixmap(str(path))
        if pixmap.isNull():
            raise ValueError("证据图无法读取")
        self.original = pixmap
        self._fit()

    def _fit(self):
        if self.original is not None:
            self.setPixmap(self.original.scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatio,
                                               Qt.TransformationMode.SmoothTransformation))

    def clear_image(self):
        self.original = None
        self.clear()
        self.setText("尚无本次任务的有效完成证据；不会显示上次任务为本次结果")

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._fit()


class FrozenScenePage(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.service = ExperimentService(self, child_filename="mic16_qt_frozen_scene_child.py")
        layout = QVBoxLayout(self)
        note = QLabel(BOUNDARY + "\n仅前半空间；阵列竖直正面：右为方位正、上为俯仰正。"
            "本页结果与实时板端会话隔离；不向板端发送命令。")
        note.setWordWrap(True)
        layout.addWidget(note)
        form = QFormLayout()
        self.scene = QComboBox()
        self.manifest_sha256 = ""
        self.catalog_error = ""
        try:
            rows, self.manifest_sha256 = catalog()
            # 保留清单顺序，包含全部无源、漏检和误报场景。
            for row in rows:
                self.scene.addItem(f"{row['scene_id']}  |  仿真真值 {row['true_source_count']} 源", row["scene_id"])
        except (OSError, ValueError, KeyError) as exc:
            self.catalog_error = str(exc)
        self.python = QLineEdit(os.environ.get("MIC16_EXPERIMENT_PYTHON", sys.executable))
        self.output = QLineEdit(str(TOOL_ROOT.parent.parent / "results/qt_frozen_replay"))
        self.config = QWidget()
        config_layout = QFormLayout(self.config)
        config_layout.setContentsMargins(0, 0, 0, 0)
        config_layout.addRow("既有冻结场景（不生成新场景）", self.scene)
        for title, field, browse in (("科研Python", self.python, self._browse_python),
                                     ("输出根目录（自动新建job）", self.output, self._browse_output)):
            row = QHBoxLayout(); row.addWidget(field, 1)
            button = QPushButton("浏览"); button.clicked.connect(browse); row.addWidget(button)
            config_layout.addRow(title, row)
        form.addRow(self.config)
        layout.addLayout(form)
        actions = QHBoxLayout()
        self.run_button = QPushButton("复现冻结场景")
        self.cancel_button = QPushButton("取消任务")
        self.open_button = QPushButton("打开本次证据目录")
        self.cancel_button.setEnabled(False); self.open_button.setEnabled(False)
        if self.catalog_error:
            self.run_button.setEnabled(False)
        self.run_button.clicked.connect(self.run)
        self.cancel_button.clicked.connect(self.service.cancel)
        self.open_button.clicked.connect(self.open_output)
        for button in (self.run_button, self.cancel_button, self.open_button):
            actions.addWidget(button)
        actions.addStretch(1); layout.addLayout(actions)
        self.status = QLabel("未运行；原HOLD结果只读。" if not self.catalog_error else "场景清单不可用：" + self.catalog_error)
        self.status.setWordWrap(True); layout.addWidget(self.status)
        self.image = EvidenceImage()
        self.detail = QPlainTextEdit()
        self.detail.setReadOnly(True); self.detail.setMaximumBlockCount(160)
        split = QSplitter(Qt.Orientation.Vertical)
        split.addWidget(self.image); split.addWidget(self.detail); split.setSizes((440, 160))
        layout.addWidget(split, 1)
        self.service.state_changed.connect(self.status.setText)
        self.service.log_changed.connect(self.detail.setPlainText)
        self.service.finished.connect(self._finished)

    def _browse_python(self):
        name, _ = QFileDialog.getOpenFileName(self, "选择科研Python", self.python.text(), "Python (python.exe python python3)")
        if name:
            self.python.setText(name)

    def _browse_output(self):
        name = QFileDialog.getExistingDirectory(self, "任务输出（提交包请选包外）", self.output.text())
        if name:
            self.output.setText(name)

    def run(self):
        try:
            if not self.output.text().strip():
                raise ValueError("输出根目录不能留空")
            request = FrozenSceneRequest(self.scene.currentData(), self.manifest_sha256, self.python.text().strip())
            self.service.start(request, Path(self.output.text()))
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, "场景未启动", str(exc))
            return
        self.image.clear_image()
        self.config.setEnabled(False); self.run_button.setEnabled(False)
        self.cancel_button.setEnabled(True); self.open_button.setEnabled(True)

    def _finished(self, receipt):
        self.config.setEnabled(True); self.run_button.setEnabled(not self.catalog_error)
        self.cancel_button.setEnabled(False)
        if receipt["status"] != "COMPLETED_TOOL_ONLY":
            self.image.clear_image()
            return
        job = Path(receipt["job_dir"])
        try:
            paths = [job / "artifacts/frozen_scene_evidence.png", job / "artifacts/localization.json",
                     job / "artifacts/scene_metadata.json"]
            for path in paths:
                limit = 8 * 1024**2 if path.suffix == ".png" else 1024**2
                if path.stat().st_size > limit or sha(path) != receipt["artifacts_sha256"].get(path.relative_to(job).as_posix()):
                    raise ValueError("完成产物大小/哈希不符，不展示可能变化的结果")
            payload = json.loads(paths[1].read_text(encoding="utf-8"))
            meta = json.loads(paths[2].read_text(encoding="utf-8"))
            from gui.multisource_contract import validate_multisource_payload
            validate_multisource_payload(payload)
            if meta.get("matches_stored_output") is not True:
                raise ValueError("缺少原冻结结果一致性检查")
            self.image.set_image(paths[0])
            lines = [f"{meta['scene_id']} | 原结果一致：是 | 状态 {payload['status']}",
                     f"仿真真值 {len(meta['truth_directions_deg'])} / 有效输出 {payload['source_count']} / 空间候选 {payload['candidate_count']}",
                     "仿真真值（不进入算法）：" + str(meta["truth_directions_deg"])]
            lines += [f"候选{c['rank']} ({c['azimuth_deg']:.2f}°, {c['elevation_deg']:.2f}°) | "
                f"{'接受' if c['accepted'] else '拒绝'} | {c['decision_reason']} | "
                f"电平余量 {c['level_gate_margin_db']:.3f}dB / 熵余量 {c['entropy_gate_margin']:.4f}"
                for c in payload["spatial_candidates"]]
            lines += [BOUNDARY, meta["display_only"], str(job)]
            self.detail.setPlainText("\n".join(lines))
            self.status.setText("复现完成，与原冻结输出一致；不是新性能验收或实板结果。")
        except (OSError, ValueError, KeyError) as exc:
            self.image.clear_image(); self.status.setText("产物显示校验失败：" + str(exc))

    def open_output(self):
        if self.service.job_dir:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.service.job_dir)))
