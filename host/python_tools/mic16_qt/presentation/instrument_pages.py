"""Qt诊断维护工具，与定位和录制路径独立。"""
from __future__ import annotations

from datetime import datetime
from pathlib import Path
import uuid

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import QTimer, Signal
from PySide6.QtWidgets import (QComboBox, QFileDialog, QGridLayout, QHBoxLayout,
    QLabel, QLineEdit, QPlainTextEdit, QPushButton, QTableWidget, QTableWidgetItem,
    QVBoxLayout, QWidget)

from mic16_qt.services.calibration_inspector import inspect_calibration
from mic16_qt.services.virtual_instrument import (EVENT_KINDS, InstrumentSnapshot,
    VirtualInstrumentObserver, export_instrument_snapshot)


class VirtualInstrumentPage(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self.observer = VirtualInstrumentObserver()
        self.layout = QVBoxLayout(self)
        controls = QHBoxLayout()
        self.channel = QComboBox()
        self.channel.addItems([f"CH{i:02d}" for i in range(16)])
        self.timebase = QComboBox()
        self.timebase.addItems([str(n) for n in (50, 100, 200, 500, 1000)])
        self.timebase.setCurrentText("200")
        self.trigger = QComboBox()
        for label, kind in (("AUDIO 帧", "AUDIO"), ("定位 RESULT", "RESULT"),
                            ("增强音频", "ENHANCED"), ("异常 ERROR", "ERROR")):
            self.trigger.addItem(label, kind)
        self.hold_button = QPushButton("运行 / 冻结")
        self.arm_button = QPushButton("单次触发")
        self.clear_button = QPushButton("清除事件")
        self.export_button = QPushButton("导出 PNG + CSV")
        for label, control in (("PCM通道", self.channel), ("窗口 / ms", self.timebase), ("触发源", self.trigger)):
            controls.addWidget(QLabel(label))
            controls.addWidget(control)
        for button in (self.hold_button, self.arm_button, self.clear_button, self.export_button):
            controls.addWidget(button)
        controls.addStretch(1)
        self.layout.addLayout(controls)
        self.state = QLabel()
        self.state.setWordWrap(True)
        self.layout.addWidget(self.state)
        # PNG不含控制按钮，但保留来源、单位和时钟边界。
        self.evidence_widget = QWidget()
        evidence = QVBoxLayout(self.evidence_widget)
        self.provenance = QLabel()
        self.provenance.setWordWrap(True)
        evidence.addWidget(self.provenance)
        self.wave_plot = pg.PlotWidget()
        self.wave_plot.setLabel("bottom", "相对最后PCM样本 / ms")
        self.wave_plot.showGrid(x=True, y=True, alpha=0.2)
        self.wave_curve = self.wave_plot.plot(pen=pg.mkPen("#49BFE3", width=1.2))
        evidence.addWidget(self.wave_plot, 3)
        self.event_plot = pg.PlotWidget()
        self.event_plot.setLabel("bottom", "相对PC派发/触发时刻 / ms（不与PCM样本严格同步）")
        self.event_plot.getAxis("left").setTicks([list(enumerate(EVENT_KINDS))])
        self.event_plot.setYRange(-0.5, 4.5, padding=0)
        self.event_curves = [self.event_plot.plot(pen=None, symbol="t", symbolSize=8,
            symbolBrush=color, symbolPen=color) for color in ("#49BFE3", "#9DAFBF", "#6ED39A", "#D889D8", "#FF8F78")]
        evidence.addWidget(self.event_plot, 2)
        self.layout.addWidget(self.evidence_widget, 1)
        self.hold_button.clicked.connect(self.toggle_hold)
        self.arm_button.clicked.connect(self.arm)
        self.clear_button.clicked.connect(self.clear_events)
        self.export_button.clicked.connect(self.choose_export)
        self.channel.currentIndexChanged.connect(self.render)
        self.timebase.currentIndexChanged.connect(self.render)
        self.refresh_timer = QTimer(self)
        self.refresh_timer.setInterval(180)
        self.refresh_timer.timeout.connect(self.refresh_visible)
        self.refresh_timer.start()
        self.render()

    def reset(self, source: str) -> None:
        self.observer.reset(source)
        self.render()

    def toggle_hold(self) -> None:
        self.observer.toggle_hold()
        self.render()

    def arm(self) -> None:
        self.observer.arm(self.trigger.currentData())
        self.render()

    def clear_events(self) -> None:
        self.observer.clear_events()
        self.render()

    def refresh_visible(self) -> None:
        if self.isVisible():
            self.render()

    def render(self, *_, snapshot: InstrumentSnapshot | None = None) -> None:
        snapshot = snapshot or self.observer.snapshot()
        channel, window = self.channel.currentIndex(), int(self.timebase.currentText())
        axis, pcm = snapshot.channel_window(channel, window)
        centered = pcm.astype(np.float64)
        if centered.size:
            centered -= centered.mean()
        rms = float(np.sqrt(np.mean(centered * centered))) if centered.size else 0.0
        peak = float(np.max(np.abs(centered))) if centered.size else 0.0
        self.state.setText(f"{snapshot.state} · CH{channel:02d} · {len(pcm)}点 · 去DC显示 RMS={rms:.6g} Peak={peak:.6g} · 事件历史淘汰 {snapshot.event_evictions} · 连续段重置 {snapshot.continuity_resets}")
        self.provenance.setText(f"PC/协议级观测 · {snapshot.source} · {snapshot.units} · {snapshot.sample_rate_hz:g} Hz\n非FPGA ILA；PCM轴与PC派发事件轴不保证样本级同步；回放/合成不是实测；CSV保留未去DC原始数值。")
        self.wave_plot.setLabel("left", "PCM去DC显示", units="counts" if snapshot.units == "int32 PCM counts" else "normalized")
        self.wave_curve.setData(axis, centered)
        self.wave_plot.setXRange(-window, 0, padding=0)
        self.event_plot.setXRange(-window, 0, padding=0)
        for lane, (kind, curve) in enumerate(zip(EVENT_KINDS, self.event_curves, strict=True)):
            xs = [(e.receive_monotonic_s - snapshot.reference_monotonic_s) * 1000 for e in snapshot.events
                  if e.kind == kind and -window <= (e.receive_monotonic_s - snapshot.reference_monotonic_s) * 1000 <= 0]
            curve.setData(xs, [lane] * len(xs))

    def choose_export(self) -> None:
        selected = QFileDialog.getExistingDirectory(self, "选择虚拟仪器导出父目录")
        if not selected:
            return
        output = Path(selected) / ("instrument_" + datetime.now().strftime("%Y%m%d_%H%M%S") + "_" + uuid.uuid4().hex[:8])
        try:
            self.export_to(output)
            self.state.setText(f"已导出：{output}（PC观测，不是ILA）")
        except (OSError, ValueError) as exc:
            self.state.setText(f"导出未完成：{exc}")

    def export_to(self, output: Path) -> Path:
        snapshot = self.observer.snapshot()
        self.render(snapshot=snapshot)
        return export_instrument_snapshot(snapshot, output, self.channel.currentIndex(),
            int(self.timebase.currentText()), lambda path: self.evidence_widget.grab().save(str(path)))


class CalibrationInspectorPage(QWidget):
    capture_requested = Signal(object)

    def __init__(self) -> None:
        super().__init__()
        self.inspection = None
        layout = QVBoxLayout(self)
        controls = QHBoxLayout()
        self.path = QLineEdit()
        self.path.setPlaceholderText("选择已有校准JSON；缺失时不自动生成理想文件")
        self.browse_button = QPushButton("浏览校准文件")
        self.reload_button = QPushButton("重新校验文件")
        self.capture_button = QPushButton("切换原始采集")
        self.legacy_reload_button = QPushButton("旧PC定位适配器重载：待迁移")
        self.legacy_reload_button.setEnabled(False)
        self.legacy_reload_button.setToolTip("旧CA-QW-SRP/CCTCN后端尚未接入Qt；文件校验不能代替算法重载")
        for control in (self.path, self.browse_button, self.reload_button, self.capture_button):
            controls.addWidget(control)
        layout.addLayout(controls)
        layout.addWidget(self.legacy_reload_button)
        self.status = QLabel("未读取校准；不代表已标定，也不改当前固件/算法。")
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        self.details = QPlainTextEdit()
        self.details.setReadOnly(True)
        self.details.setMaximumBlockCount(80)
        layout.addWidget(self.details, 1)
        self.channels = QTableWidget(16, 3)
        self.channels.setHorizontalHeaderLabels(("通道（零基编号）", "固定延时 / samples", "文件声明健康掩码"))
        self.channels.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.channels.horizontalHeader().setStretchLastSection(True)
        layout.addWidget(self.channels, 2)
        self.browse_button.clicked.connect(self.browse)
        self.reload_button.clicked.connect(self.reload)
        self.capture_button.clicked.connect(lambda: self.capture_requested.emit(((1, 0),)))
        self.clear_values()
        self.channels.resizeColumnsToContents()

    def clear_values(self) -> None:
        self.inspection = None
        for row in range(16):
            for column, value in enumerate((f"CH{row:02d}", "未知", "未知")):
                self.channels.setItem(row, column, QTableWidgetItem(value))

    def browse(self) -> None:
        name, _ = QFileDialog.getOpenFileName(self, "只读检查校准JSON", "", "JSON (*.json)")
        if name:
            self.path.setText(name)
            self.reload()

    def reload(self) -> bool:
        self.clear_values()
        try:
            if not self.path.text().strip():
                raise ValueError("请先选择校准JSON")
            result = inspect_calibration(Path(self.path.text().strip()))
        except (OSError, ValueError, OverflowError, RecursionError) as exc:
            self.status.setText(f"读取/校验失败：{exc}；未应用任何参数。")
            self.details.clear()
            return False
        self.inspection = result
        self.status.setText(("字段结构通过" if result.structurally_valid else "字段结构未通过") + "；来源未认证、未应用算法、未上传板端。")
        lines = [f"文件SHA256：{result.sha256}", f"版本：{result.version}",
                 f"阵列零度偏移：{result.azimuth_offset_deg} deg", ""]
        lines += ["错误：" + x for x in result.issues] + ["说明：" + x for x in result.warnings]
        if result.pair_health:
            lines += ["32对健康权重（文件声明，不是实物健康测量）："]
            lines += [f"pair[{i:02d}:{i+7:02d}] " + " / ".join(f"{v:.6g}" for v in result.pair_health[i:i+8])
                      for i in range(0, 32, 8)]
        self.details.setPlainText("\n".join(lines))
        for row in range(16):
            if result.channel_delays:
                self.channels.item(row, 1).setText(f"{result.channel_delays[row]:.6g}")
            if result.channel_health:
                self.channels.item(row, 2).setText(str(result.channel_health[row]))
        self.channels.resizeColumnsToContents()
        return result.structurally_valid
