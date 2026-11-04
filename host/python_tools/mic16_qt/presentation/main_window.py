from __future__ import annotations

import json
import time
from datetime import datetime
from pathlib import Path

from PySide6.QtCore import QThread, Qt, QTimer, Signal
from PySide6.QtGui import QCloseEvent
from PySide6.QtWidgets import (
    QAbstractItemView,
    QButtonGroup,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QSplitter,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from mic16_qt import APP_VERSION
from mic16_config import OFFICIAL_SAMPLE_RATE
from mic16_qt.core.models import AcousticFrame, EnhancedAudioUpdate, LocalizationUpdate
from mic16_udp_common import Mic16Status, Mic16RtosStatus, Mic16CommandAck
from mic16_qt.presentation.widgets import (
    HistorySparkline,
    MetricCard,
    SectionFrame,
    SoundFieldView,
    SourceTable,
    SpectrumView,
    WaveformComparisonView,
    WaveformView,
)
from mic16_qt.services.synthetic_source import SyntheticAcousticSource
from mic16_qt.services.audio_sources import ReplayAcousticSource
from mic16_qt.services.recorder import RecordingArtifacts
from mic16_qt.services.recording_service import RecordingService
from mic16_qt.presentation.recording_labels_panel import RecordingLabelsPanel
from mic16_qt.services.udp_worker import UdpReceiverWorker
from mic16_qt.services.board_protocol import WdasDiagnosticsReceipt
from mic16_qt.presentation.instrument_pages import VirtualInstrumentPage, CalibrationInspectorPage
from mic16_qt.presentation.experiment_page import OfflineExperimentPage
from mic16_qt.presentation.frozen_scene_page import FrozenScenePage


class StatusCell(QWidget):
    def __init__(self, label: str, value: str, accent: str = "#007E9E") -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)
        layout.setSpacing(2)
        title = QLabel(label)
        title.setObjectName("MetricLabel")
        self.value = QLabel(value)
        self.value.setStyleSheet(f"font-weight: 700; color: {accent};")
        layout.addWidget(title)
        layout.addWidget(self.value)


class PageBase(QWidget):
    def __init__(self, title: str, subtitle: str) -> None:
        super().__init__()
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(18, 15, 18, 16)
        self.layout.setSpacing(12)
        title_label = QLabel(title)
        title_label.setObjectName("PageTitle")
        subtitle_label = QLabel(subtitle)
        subtitle_label.setObjectName("PageSubtitle")
        subtitle_label.setWordWrap(True)
        self.layout.addWidget(title_label)
        self.layout.addWidget(subtitle_label)


class OverviewPage(PageBase):
    def __init__(self) -> None:
        super().__init__("系统总览", "面向演示和运行值守的端到端摘要；当前数据源为确定性离线仿真。")
        metrics = QHBoxLayout()
        metrics.setSpacing(10)
        self.channel_metric = MetricCard("通道健康", "16 / 16", "模拟阵列输入")
        self.source_metric = MetricCard("有效声源", "--", "匿名源，不识别身份")
        self.latency_metric = MetricCard("处理延迟", "--", "界面链路估计")
        self.loss_metric = MetricCard("数据连续性", "0 丢包", "Synthetic source")
        for card in (self.channel_metric, self.source_metric, self.latency_metric, self.loss_metric):
            metrics.addWidget(card, 1)
        self.layout.addLayout(metrics)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        waveform_panel = SectionFrame("阵列波形", "CH01 / 05 / 09 / 13 · 约 20 FPS")
        self.waveform = WaveformView()
        waveform_panel.body.addWidget(self.waveform)
        field_panel = SectionFrame("二维声场证据", "Az / El · dB · 有效源 × / 候选 ○")
        self.sound_field = SoundFieldView()
        field_panel.body.addWidget(self.sound_field)
        splitter.addWidget(waveform_panel)
        splitter.addWidget(field_panel)
        splitter.setSizes((650, 660))
        self.layout.addWidget(splitter, 1)

    def update_frame(self, frame: AcousticFrame) -> None:
        self.waveform.update_frame(frame)
        self.sound_field.update_frame(frame)
        self.source_metric.set_value(str(len(frame.sources)), frame.algorithm_name)
        self.latency_metric.set_value(f"{frame.latency_ms:.1f} ms", "UI synthetic pipeline")
        self.loss_metric.set_value(f"{frame.packet_loss} 丢包", f"Frame {frame.frame_index:06d}")

    def update_localization(self, update: LocalizationUpdate) -> None:
        self.sound_field.update_sources(update.sources)
        self.source_metric.set_value(str(len(update.sources)), update.algorithm_name)
        self.latency_metric.set_value(f"{update.latency_ms:.1f} ms", "板端结果 payload")


class CapturePage(PageBase):
    record_start_requested = Signal()
    record_stop_requested = Signal()
    record_export_requested = Signal()

    def __init__(self) -> None:
        super().__init__("实时采集", "16 通道数据、PSD 与连续性监视。录音标签在开始时固定，录制/保存/导出在后台执行；显示抽帧不等于录音丢包。")
        actions = QHBoxLayout()
        self.record_start_button = _button("开始录制", primary=True)
        self.record_stop_button = _button("停止并保存", primary=False)
        self.record_export_button = _button("导出 WAV / MAT", primary=False)
        self.record_status = QLabel("未录制")
        self.record_status.setObjectName("PageSubtitle")
        self.record_status.setWordWrap(True)
        self.record_stop_button.setEnabled(False)
        self.record_export_button.setEnabled(False)
        self.record_start_button.clicked.connect(self.record_start_requested)
        self.record_stop_button.clicked.connect(self.record_stop_requested)
        self.record_export_button.clicked.connect(self.record_export_requested)
        actions.addWidget(self.record_start_button)
        actions.addWidget(self.record_stop_button)
        actions.addWidget(self.record_export_button)
        actions.addSpacing(12)
        actions.addStretch(1)
        self.layout.addLayout(actions)
        self.layout.addWidget(self.record_status)
        splitter = QSplitter(Qt.Orientation.Vertical)
        wave_panel = SectionFrame("16 通道 PCM", "矩阵视图 · 数据仅用于界面链路验证")
        self.waveform = WaveformView(tuple(range(16)), history_samples=2048)
        wave_panel.body.addWidget(self.waveform)
        spectrum_panel = SectionFrame("输入频谱", "CH01 · 0–4 kHz")
        self.spectrum = SpectrumView(show_enhanced=False)
        spectrum_panel.body.addWidget(self.spectrum)
        splitter.addWidget(wave_panel)
        splitter.addWidget(spectrum_panel)
        splitter.setSizes((520, 260))
        self.tabs = QTabWidget()
        self.tabs.addTab(splitter, "波形与频谱")
        self.labels = RecordingLabelsPanel()
        label_scroll = QScrollArea()
        label_scroll.setWidgetResizable(True)
        label_scroll.setWidget(self.labels)
        self.tabs.addTab(label_scroll, "录音标签（开始时固定）")
        self.layout.addWidget(self.tabs, 1)

    def update_frame(self, frame: AcousticFrame) -> None:
        self.waveform.update_frame(frame)
        self.spectrum.update_frame(frame)

    def set_recording_state(self, active: bool, text: str, has_export: bool = False, busy: bool = False) -> None:
        self.record_start_button.setEnabled(not active and not busy)
        self.record_stop_button.setEnabled(active)
        self.record_export_button.setEnabled((not active) and not busy and has_export)
        self.record_status.setText(text)
        self.labels.set_locked(active or busy)


class LocalizationPage(PageBase):
    def __init__(self) -> None:
        super().__init__("空间定位", "冻结 v1 的匿名有效声源视图；原始候选与最终输出分层呈现。")
        splitter = QSplitter(Qt.Orientation.Horizontal)
        field_panel = SectionFrame("二维 Az / El 响应", "颜色为归一化证据，不等同 SPL")
        self.sound_field = SoundFieldView()
        field_panel.body.addWidget(self.sound_field)
        side = QWidget()
        side_layout = QVBoxLayout(side)
        side_layout.setContentsMargins(0, 0, 0, 0)
        table_panel = SectionFrame("有效声源列表", "mic16.multisource.localization.v1")
        self.source_table = SourceTable()
        table_panel.body.addWidget(self.source_table)
        history_panel = SectionFrame("主导源轨迹", "Az / El 历史")
        self.history = HistorySparkline()
        history_panel.body.addWidget(self.history)
        side_layout.addWidget(table_panel)
        side_layout.addWidget(history_panel, 1)
        splitter.addWidget(field_panel)
        splitter.addWidget(side)
        splitter.setSizes((830, 440))
        self.layout.addWidget(splitter, 1)

    def update_frame(self, frame: AcousticFrame) -> None:
        self.sound_field.update_frame(frame)
        self.source_table.update_frame(frame)
        self.history.update_frame(frame)

    def clear_protocol_view(self) -> None:
        self.sound_field.clear_evidence()
        self.source_table.update_sources(())
        self.sound_field.update_sources(())
        self.history.update_sources(())

    def update_localization(self, update: LocalizationUpdate) -> None:
        self.sound_field.update_sources(update.sources)
        self.source_table.update_sources(update.sources)
        self.history.update_sources(update.sources)


class EnhancementPage(PageBase):
    def __init__(self) -> None:
        super().__init__("语音增强", "保留 WDAS 主线，清楚区分原始阵列输入与增强输出；本页当前为合成链路。")
        metrics = QHBoxLayout()
        self.input_metric = MetricCard("输入 RMS", "--", "CH01")
        self.output_metric = MetricCard("输出 RMS", "--", "WDAS")
        self.gain_metric = MetricCard("估计 SNR 增益", "--", "Synthetic only")
        for card in (self.input_metric, self.output_metric, self.gain_metric):
            metrics.addWidget(card, 1)
        self.layout.addLayout(metrics)
        splitter = QSplitter(Qt.Orientation.Vertical)
        wave_panel = SectionFrame("增强前后波形", "归一化显示，不改变原始导出")
        self.waveform = WaveformComparisonView()
        wave_panel.body.addWidget(self.waveform)
        spectrum_panel = SectionFrame("增强前后频谱", "Raw CH01 / WDAS")
        self.spectrum = SpectrumView(show_enhanced=True)
        spectrum_panel.body.addWidget(self.spectrum)
        splitter.addWidget(wave_panel)
        splitter.addWidget(spectrum_panel)
        splitter.setSizes((420, 330))
        self.layout.addWidget(splitter, 1)

    def update_frame(self, frame: AcousticFrame) -> None:
        self.input_metric.set_value(f"{frame.input_rms_dbfs:.1f} dBFS", "原始 CH01")
        if frame.algorithm_name.startswith("Q2 采集链路"):
            self.waveform.update_raw(frame.pcm[:, 0], frame.sample_rate_hz, frame_index=frame.frame_index,
                                     evidence_source=frame.evidence_source, packet_loss=frame.packet_loss)
            self.spectrum.update_raw(frame.frequency_hz, frame.raw_spectrum_db)
            self.output_metric.set_value("--", "等待 ENHANCED_AUDIO")
            self.gain_metric.set_value("--", "无增强结果，不作推断")
            return
        self.waveform.update_frame(frame)
        self.spectrum.update_frame(frame)
        self.output_metric.set_value(f"{frame.output_rms_dbfs:.1f} dBFS", "增强输出")
        self.gain_metric.set_value(f"+{frame.snr_gain_db:.1f} dB", "非板端实测")

    def update_enhanced(self, update: EnhancedAudioUpdate) -> None:
        self.waveform.update_enhanced(update.samples, update.sample_rate_hz, frame_index=update.frame_index,
                                      evidence_source=update.evidence_source)
        self.spectrum.update_enhanced(update.frequency_hz, update.spectrum_db)
        self.output_metric.set_value(f"{update.output_rms_dbfs:.1f} dBFS", "板端 WDAS 输出")
        self.gain_metric.set_value("--", "未同时获得参考真值")


class ExperimentPage(PageBase):
    export_requested = Signal()
    replay_requested = Signal()

    def __init__(self) -> None:
        super().__init__("实验工作台", "离线回放、场景实验、结果归档和论文图导出在同一 session 下追溯。")
        actions = QHBoxLayout()
        actions.setSpacing(9)
        self.replay_button = _button("加载回放", primary=False)
        self.scene_button = _button("冻结二维场景复现", primary=False)
        self.export_button = _button("导出当前证据包", primary=True)
        self.replay_button.setToolTip("选择 raw_i32.npy，按最长有效片段循环回放")
        self.scene_button.setToolTip("进入只读冻结场景复现；不生成新HOLD、不调整阈值")
        self.export_button.clicked.connect(self.export_requested)
        self.replay_button.clicked.connect(self.replay_requested)
        actions.addWidget(self.replay_button)
        actions.addWidget(self.scene_button)
        actions.addWidget(self.export_button)
        actions.addStretch(1)
        self.layout.addLayout(actions)

        self.tabs = QTabWidget()
        self.offline = OfflineExperimentPage()
        self.tabs.addTab(self.offline, "离线任务（旧入口）")
        self.frozen = FrozenScenePage()
        self.tabs.addTab(self.frozen, "冻结二维场景复现")
        self.scene_button.clicked.connect(lambda: self.tabs.setCurrentWidget(self.frozen))
        panel = SectionFrame("迁移与证据状态", "Q4-B 部分接入 / 四个旧离线任务已接")
        table = QTableWidget(6, 4)
        table.setHorizontalHeaderLabels(("能力域", "当前源", "Qt 状态", "下一门禁"))
        table.verticalHeader().setVisible(False)
        table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        rows = (
            ("16ch 波形/频谱", "Synthetic/Replay/UDP", "Q2 已接", "真实板端验收"),
            ("二维匿名多源", "0x0008 versioned payload", "Q3 已接", "真实板端验收"),
            ("WDAS 输出", "0x0004 chunked PCM", "Q3 已接", "真实板端验收"),
            ("离线回放", "raw_i32 + manifest", "Q2 已接", "长时回放"),
            ("虚拟仪器/校准/任务", "PC观测/只读校准/冻结复现", "Q4-B 部分已接", "旧PC重载/自定义二维待迁移"),
            ("NPY/WAV/MAT/清单", "Qt recorder + exporter", "Q4-A 已接", "真实板端长时验收"),
        )
        for row, values in enumerate(rows):
            for column, value in enumerate(values):
                table.setItem(row, column, QTableWidgetItem(value))
        table.horizontalHeader().setStretchLastSection(True)
        table.resizeColumnsToContents()
        panel.body.addWidget(table)
        self.tabs.addTab(panel, "迁移与证据状态")
        self.layout.addWidget(self.tabs, 1)


class DiagnosticsPage(PageBase):
    def __init__(self) -> None:
        super().__init__("诊断维护", "虚拟逻辑分析仪不会冒充 Vivado ILA；当前只展示软件事件与数据合同。")
        self.wdas_diagnostics = QLabel("WDAS 诊断：未知，尚未收到 0x000B 快照（旧固件可能不支持）。")
        self.wdas_diagnostics.setWordWrap(True)
        self.tabs = QTabWidget()
        runtime_page = QWidget()
        runtime_layout = QVBoxLayout(runtime_page)
        runtime_layout.addWidget(self.wdas_diagnostics)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        contract_panel = SectionFrame("当前数据合同", "只读")
        self.contract_text = QPlainTextEdit()
        self.contract_text.setReadOnly(True)
        self.contract_text.setPlainText(
            "证据来源：SYNTHETIC（合成演示，非实测）\n"
            "多声源协议：mic16.multisource.localization.v1\n"
            "坐标约定：方位角右正，俯仰角上正\n"
            "算法：冻结 v1 / TF-zone + WDAS\n"
            "实板真值：暂无硬件\n"
            "Vivado ILA：独立硬件证据，不由本页替代\n"
        )
        contract_panel.body.addWidget(self.contract_text)
        log_panel = SectionFrame("运行事件", "界面线程事件摘要")
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(400)
        self.log.appendPlainText("[BOOT] MIC16 Qt Q4-B 部分功能初始化")
        self.log.appendPlainText("[SOURCE] 初始来源为合成演示，非实测")
        log_panel.body.addWidget(self.log)
        splitter.addWidget(contract_panel)
        splitter.addWidget(log_panel)
        splitter.setSizes((510, 760))
        runtime_layout.addWidget(splitter, 1)
        self.instrument = VirtualInstrumentPage()
        self.calibration = CalibrationInspectorPage()
        self.tabs.addTab(runtime_page, "运行状态与合同")
        self.tabs.addTab(self.instrument, "虚拟仪器（PC观测）")
        self.tabs.addTab(self.calibration, "校准文件检查")
        self.layout.addWidget(self.tabs, 1)

    def update_frame(self, frame: AcousticFrame) -> None:
        if frame.frame_index % 50 == 0:
            self.log.appendPlainText(
                f"[FRAME {frame.frame_index:06d}] sources={len(frame.sources)} "
                f"latency={frame.latency_ms:.1f}ms loss={frame.packet_loss}"
            )

    def set_source(self, source: str, detail: str) -> None:
        self.contract_text.setPlainText(
            f"证据来源       : {source}\n"
            "多声源协议     : mic16.multisource.localization.v1（Q3 已接）\n"
            "录制与导出     : NPY/WAV/MAT + SHA256 清单（Q4-A 已接）\n"
            "坐标约定       : 方位角右正、俯仰角上正\n"
            f"当前链路       : {detail}\n"
            "板端定位结论   : 仅显示通过 0x0008 校验的正式输出\n"
            "Vivado ILA     : 独立硬件证据，不由本页替代\n"
        )
        self.log.appendPlainText(f"[SOURCE] {source} · {detail}")


class SettingsPage(PageBase):
    apply_source_requested = Signal()
    board_command_requested = Signal(object)

    def __init__(self) -> None:
        super().__init__("设置", "工程参数与板端控制；仅在 UDP 监听会话中发送，收到 ACK 不等于硬件验收。")
        panel = SectionFrame("连接与刷新", "Q2 数据源切换")
        form = QFormLayout()
        form.setSpacing(12)
        self.source = QComboBox()
        self.source.addItems(("离线演示 / SYNTHETIC", "离线回放 / REPLAY", "板端 UDP / BOARD LIVE"))
        self.board_ip = QLineEdit("192.168.1.10")
        self.host_ip = QLineEdit("0.0.0.0")
        self.port = QSpinBox()
        self.port.setRange(1, 65535)
        self.port.setValue(5000)
        self.refresh = QSpinBox()
        self.refresh.setRange(5, 30)
        self.refresh.setValue(10)
        self.refresh.setSuffix(" FPS")
        form.addRow("数据源", self.source)
        form.addRow("板卡 IP / 控制目标", self.board_ip)
        form.addRow("本机监听地址", self.host_ip)
        form.addRow("UDP 端口", self.port)
        form.addRow("图形刷新", self.refresh)
        form_widget = QWidget()
        form_widget.setLayout(form)
        form_widget.setMaximumWidth(620)
        panel.body.addWidget(form_widget)
        apply_button = _button("应用数据源", primary=True)
        apply_button.clicked.connect(self.apply_source_requested)
        panel.body.addWidget(apply_button, 0, Qt.AlignmentFlag.AlignLeft)
        control_row = QHBoxLayout()
        controls = (
            ("原始采集", ((1, 0),)),
            ("二维定位", ((6, 1), (1, 1))),
            ("定位 + WDAS 片段", ((6, 2), (1, 1))),
            ("读取状态", ((2, 0),)),
            ("停采", ((4, 0),)),
            ("启采", ((4, 1),)),
        )
        for label, commands in controls:
            button = _button(label, primary=False)
            button.clicked.connect(lambda checked=False, commands=commands: self.board_command_requested.emit(commands))
            control_row.addWidget(button)
        panel.body.addLayout(control_row)
        self.board_feedback = QLabel("未收到板端状态；模拟源与回放不会发送控制命令。")
        self.board_feedback.setWordWrap(True)
        panel.body.addWidget(self.board_feedback)
        panel.body.addStretch(1)
        self.layout.addWidget(panel, 1)


class MainWindow(QMainWindow):
    def __init__(
        self,
        autostart: bool = True,
        source_mode: str = "synthetic",
        replay_path: Path | None = None,
        listen_ip: str = "0.0.0.0",
        port: int = 5000,
    ) -> None:
        super().__init__()
        self.setWindowTitle("MIC16 阵列声学研究与验证平台")
        self.resize(1500, 940)
        self.setMinimumSize(1180, 760)
        self._source = SyntheticAcousticSource()
        self._source_mode = "synthetic"
        self._last_frame: AcousticFrame | None = None
        self._last_localization: LocalizationUpdate | None = None
        self._last_enhanced: EnhancedAudioUpdate | None = None
        self._recorder = RecordingService(self)
        self._recorder.progress_changed.connect(self._recording_progress)
        self._recorder.finished.connect(self._recording_finished)
        self._close_pending = False
        self._last_recording: RecordingArtifacts | None = None
        self._last_diagnostics: WdasDiagnosticsReceipt | None = None
        self._udp_thread: QThread | None = None
        self._udp_worker: UdpReceiverWorker | None = None
        self._listen_ip = str(listen_ip)
        self._port = int(port)
        self._timer = QTimer(self)
        self._diagnostics_timer = QTimer(self)
        self._diagnostics_timer.setInterval(1000)
        self._diagnostics_timer.timeout.connect(self._check_diagnostics_age)
        self._diagnostics_timer.start()
        self._timer.setInterval(100)
        self._timer.timeout.connect(self._tick)
        self._udp_display_timer = QTimer(self)
        self._udp_display_timer.timeout.connect(self._poll_udp_display)
        self._udp_display_overflow = 0
        self._pages: list[QWidget] = []
        self._build_ui()
        if source_mode == "replay" and replay_path is not None:
            self.load_replay(replay_path, show_errors=False)
        elif source_mode == "udp":
            self.start_udp(self._listen_ip, self._port)
        else:
            self.start_synthetic(autostart=autostart)

    def _build_ui(self) -> None:
        root = QWidget()
        root.setObjectName("AppRoot")
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)
        root_layout.addWidget(self._build_header())
        root_layout.addWidget(self._build_status_strip())

        work = QWidget()
        work_layout = QHBoxLayout(work)
        work_layout.setContentsMargins(0, 0, 0, 0)
        work_layout.setSpacing(0)
        work_layout.addWidget(self._build_navigation())
        self.stack = QStackedWidget()
        self.overview_page = OverviewPage()
        self.capture_page = CapturePage()
        self.localization_page = LocalizationPage()
        self.enhancement_page = EnhancementPage()
        self.experiment_page = ExperimentPage()
        self.diagnostics_page = DiagnosticsPage()
        self.settings_page = SettingsPage()
        self._pages = [
            self.overview_page,
            self.capture_page,
            self.localization_page,
            self.enhancement_page,
            self.experiment_page,
            self.diagnostics_page,
            self.settings_page,
        ]
        for page in self._pages:
            self.stack.addWidget(page)
        self.stack.currentChanged.connect(lambda index: self._navigation_group.button(index).setChecked(True))
        work_layout.addWidget(self.stack, 1)
        root_layout.addWidget(work, 1)
        self.setCentralWidget(root)
        self.experiment_page.export_requested.connect(self.export_evidence_pack)
        self.experiment_page.replay_requested.connect(self.choose_replay)
        self.capture_page.record_start_requested.connect(self.choose_recording_directory)
        self.capture_page.record_stop_requested.connect(self.stop_recording)
        self.capture_page.record_export_requested.connect(self.export_last_recording)
        self.settings_page.apply_source_requested.connect(self.apply_settings_source)
        self.settings_page.board_command_requested.connect(self.send_board_commands)
        self.diagnostics_page.calibration.capture_requested.connect(self.send_board_commands)
        self.statusBar().showMessage(f"MIC16 Qt {APP_VERSION} · 旧离线任务/虚拟仪器/只读校准已接；完整迁移未完成")

    def _build_header(self) -> QWidget:
        header = QWidget()
        header.setObjectName("TopHeader")
        layout = QHBoxLayout(header)
        layout.setContentsMargins(20, 13, 20, 13)
        text = QVBoxLayout()
        text.setSpacing(2)
        kicker = QLabel("MIC16 · ACOUSTIC ARRAY ENGINEERING CONSOLE")
        kicker.setObjectName("ProductKicker")
        title = QLabel("阵列声学研究与验证平台")
        title.setObjectName("ProductTitle")
        subtitle = QLabel("16 通道采集  /  二维匿名多源  /  WDAS 空间增强  /  实验证据管理")
        subtitle.setObjectName("ProductSubtitle")
        text.addWidget(kicker)
        text.addWidget(title)
        text.addWidget(subtitle)
        layout.addLayout(text)
        layout.addStretch(1)
        self.pause_button = _button("暂停演示", primary=False)
        self.pause_button.clicked.connect(self.toggle_running)
        self.export_button = _button("导出证据", primary=True)
        self.export_button.clicked.connect(self.export_evidence_pack)
        self.evidence_badge = QLabel("SYNTHETIC\n非板端实测")
        self.evidence_badge.setObjectName("EvidenceBadge")
        self.evidence_badge.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.pause_button)
        layout.addWidget(self.export_button)
        layout.addSpacing(8)
        layout.addWidget(self.evidence_badge)
        return header

    def _build_status_strip(self) -> QWidget:
        strip = QWidget()
        strip.setObjectName("StatusStrip")
        layout = QHBoxLayout(strip)
        layout.setContentsMargins(10, 4, 10, 4)
        layout.setSpacing(0)
        self.link_status = StatusCell("连接", "模拟源在线", "#178C69")
        self.mode_status = StatusCell("模式", "离线演示")
        self.source_status = StatusCell("结果来源", "SYNTHETIC", "#B77600")
        self.stream_status = StatusCell("数据连续性", "等待首帧")
        self.config_status = StatusCell("配置一致性", "Q4-A / 等待板端 CRC", "#61768A")
        for cell in (
            self.link_status,
            self.mode_status,
            self.source_status,
            self.stream_status,
            self.config_status,
        ):
            layout.addWidget(cell, 1)
        return strip

    def _build_navigation(self) -> QWidget:
        nav = QWidget()
        nav.setObjectName("SideNav")
        nav.setFixedWidth(176)
        layout = QVBoxLayout(nav)
        layout.setContentsMargins(10, 14, 10, 14)
        layout.setSpacing(5)
        names = (
            "01  系统总览",
            "02  实时采集",
            "03  空间定位",
            "04  语音增强",
            "05  实验工作台",
            "06  诊断维护",
            "07  设置",
        )
        group = QButtonGroup(self)
        group.setExclusive(True)
        self._navigation_group = group
        for index, name in enumerate(names):
            button = QPushButton(name)
            button.setObjectName("NavButton")
            button.setCheckable(True)
            button.setMinimumHeight(42)
            button.clicked.connect(lambda checked=False, page=index: self.stack.setCurrentIndex(page))
            group.addButton(button, index)
            layout.addWidget(button)
            if index == 0:
                button.setChecked(True)
        layout.addStretch(1)
        version = QLabel(f"Qt Q4-B 部分接入\n{APP_VERSION}")
        version.setStyleSheet("color: #8EA9BD; font-family: Consolas; font-size: 11px;")
        layout.addWidget(version)
        return nav

    def _tick(self) -> None:
        if self._source is None:
            return
        try:
            frame = self._source.next_frame()
        except Exception as exc:
            self._timer.stop()
            self.diagnostics_page.log.appendPlainText(f"[ERROR] {exc}")
            self.statusBar().showMessage(f"数据源错误：{exc}", 12_000)
            return
        self._apply_frame(frame)

    def _apply_frame(self, frame: AcousticFrame, *, record_raw=True) -> None:
        if not self._accept_udp_callback():
            return
        self._last_frame = frame
        if record_raw:
            self._record_frame(frame)
        try:
            self.diagnostics_page.instrument.observer.observe_frame(frame)
        except (ValueError, TypeError) as exc:
            # 观测为独立支路，不能停止录制或处理。
            self.diagnostics_page.log.appendPlainText(f"[INSTRUMENT REJECT] {exc}")
        for page in (
            self.overview_page,
            self.capture_page,
            self.localization_page,
            self.enhancement_page,
            self.diagnostics_page,
        ):
            page.update_frame(frame)
        self.stream_status.value.setText(f"连续 · F{frame.frame_index:06d}")
        self.source_status.value.setText(frame.evidence_source)
        if frame.evidence_source == "BOARD LIVE":
            self.stream_status.value.setText(
                f"F{frame.frame_index:06d} · 丢包 {frame.packet_loss}"
            )

    def _record_frame(self, frame: AcousticFrame) -> None:
        """追加一帧原始数据，避免录制测试依赖绘图过程。"""

        if self._recorder is not None and self._recorder.active:
            self._recorder.append(frame)

    def start_synthetic(self, *, autostart: bool = True) -> None:
        self._finish_recording_for_source_change()
        self._stop_udp()
        self._source = SyntheticAcousticSource()
        self._source_mode = "synthetic"
        self.settings_page.source.setCurrentIndex(0)
        self._timer.setInterval(max(1, round(1000 / self.settings_page.refresh.value())))
        if autostart:
            self._timer.start()
        self._set_source_identity("SYNTHETIC", "离线演示", "模拟源在线", "确定性界面测试源")
        self._set_running(autostart)
        QTimer.singleShot(0, self._tick)

    def choose_replay(self) -> None:
        path, _filter = QFileDialog.getOpenFileName(
            self,
            "选择 MIC16 raw_i32 回放文件",
            str(Path(__file__).resolve().parents[3] / "results"),
            "NumPy 录音 (raw_i32.npy *.npy)",
        )
        if path:
            self.load_replay(Path(path))

    def load_replay(self, path: Path, *, show_errors: bool = True) -> bool:
        try:
            source = ReplayAcousticSource(Path(path), frame_samples=512)
        except Exception as exc:
            if show_errors:
                QMessageBox.warning(self, "回放文件不可用", str(exc))
            self.statusBar().showMessage(f"回放加载失败：{exc}", 12_000)
            return False
        self._finish_recording_for_source_change()
        self._stop_udp()
        self._source = source
        self._source_mode = "replay"
        self.settings_page.source.setCurrentIndex(1)
        self._timer.setInterval(max(1, round(1000 / self.settings_page.refresh.value())))
        self._timer.start()
        self._set_source_identity("REPLAY", "离线回放", "回放文件在线", source.path.name)
        self._set_running(True)
        self.statusBar().showMessage(f"已加载回放：{source.path}", 10_000)
        return True

    def start_udp(self, listen_ip: str, port: int) -> None:
        self._finish_recording_for_source_change()
        self._timer.stop()
        self._stop_udp()
        self._source = None
        self._source_mode = "udp"
        self._listen_ip = str(listen_ip)
        self._port = int(port)
        self.settings_page.source.setCurrentIndex(2)
        self.settings_page.host_ip.setText(self._listen_ip)
        self.settings_page.port.setValue(self._port)
        thread = QThread(self)
        worker = UdpReceiverWorker(self._listen_ip, self._port, frame_samples=512, coalesce_display=True)
        worker.moveToThread(thread)
        thread.started.connect(worker.run)
        worker.frame_ready.connect(self._apply_frame)
        worker.localization_ready.connect(self._apply_localization)
        worker.enhanced_ready.connect(self._apply_enhanced)
        worker.status_ready.connect(self._apply_board_status)
        worker.command_ack_ready.connect(self._apply_command_ack)
        worker.diagnostics_ready.connect(self._apply_wdas_diagnostics)
        worker.state_changed.connect(self._on_udp_state)
        worker.receive_error.connect(self._on_udp_error)
        worker.finished.connect(thread.quit, Qt.ConnectionType.DirectConnection)
        thread.finished.connect(worker.deleteLater)
        thread.finished.connect(thread.deleteLater)
        self._udp_thread = thread
        self._udp_worker = worker
        self._udp_display_overflow = 0
        self._udp_display_timer.start(max(1, round(1000 / self.settings_page.refresh.value())))
        self._last_localization = None
        self._last_enhanced = None
        self.overview_page.sound_field.clear_evidence()
        self.localization_page.clear_protocol_view()
        self._set_source_identity("BOARD LIVE", "板端 UDP", "等待数据", f"监听 {self._listen_ip}:{self._port}")
        self._set_running(True)
        thread.start()

    def _poll_udp_display(self):
        worker = self._udp_worker
        if self._source_mode != "udp" or worker is None:
            return
        events, counts = worker.take_display_snapshot()
        for event in events:
            if isinstance(event, AcousticFrame): self._apply_frame(event, record_raw=False)
            elif isinstance(event, LocalizationUpdate): self._apply_localization(event)
            elif isinstance(event, EnhancedAudioUpdate): self._apply_enhanced(event)
            elif isinstance(event, (Mic16Status, Mic16RtosStatus)): self._apply_board_status(event)
            elif isinstance(event, Mic16CommandAck): self._apply_command_ack(event)
            elif isinstance(event, WdasDiagnosticsReceipt): self._apply_wdas_diagnostics(event, record_raw=False)
        skipped = counts["display_replaced_pcm_frames"]
        self.capture_page.waveform.setToolTip(f"显示按刷新率抽帧：累计省略 {skipped} 帧；录制独立保存全部已接受PCM，不是UDP丢包数。")
        if counts["ack_display_overflow"] > self._udp_display_overflow:
            self._udp_display_overflow = counts["ack_display_overflow"]
            self.settings_page.board_feedback.setText("ACK显示队列超限；部分确认未展示，请重新读取状态，不能默认命令成功。")

    def apply_settings_source(self) -> None:
        index = self.settings_page.source.currentIndex()
        if index == 0:
            self.start_synthetic()
        elif index == 1:
            self.choose_replay()
        else:
            self.start_udp(self.settings_page.host_ip.text().strip() or "0.0.0.0", self.settings_page.port.value())

    def _accept_udp_callback(self) -> bool:
        # Qt队列信号保留sender()；停止、重连或切换数据源后
        # 忽略旧接收器；直接测试/演示调用没有发送者。
        sender = self.sender()
        return not isinstance(sender, UdpReceiverWorker) or (
            self._source_mode == "udp" and sender is self._udp_worker)

    def _on_udp_state(self, state: str) -> None:
        if not self._accept_udp_callback():
            return
        self.link_status.value.setText("UDP 监听中")
        self.stream_status.value.setText("等待 AUDIO")
        self.diagnostics_page.log.appendPlainText(f"[UDP] {state}")

    def _on_udp_error(self, message: str) -> None:
        if not self._accept_udp_callback():
            return
        if self._source_mode == "udp":
            self.diagnostics_page.instrument.observer.observe_event("ERROR", detail=message)
        self.link_status.value.setText("UDP 异常")
        self.stream_status.value.setText("接收失败")
        self.diagnostics_page.log.appendPlainText(f"[UDP ERROR] {message}")
        self.statusBar().showMessage(f"UDP 错误：{message}", 12_000)

    def send_board_commands(self, commands: tuple[tuple[int, int], ...]) -> None:
        if self._source_mode != "udp" or self._udp_worker is None:
            self.settings_page.board_feedback.setText("请先应用板端 UDP 数据源；本次未发送。")
            return
        try:
            for command, arg0 in commands:
                if not self._udp_worker.send_command(self.settings_page.board_ip.text().strip(),
                                                     self.settings_page.port.value(), command, arg0):
                    raise ValueError("控制发送队列已满，请等待 ACK 后再操作")
        except ValueError as exc:
            self.settings_page.board_feedback.setText(str(exc))
            return
        self.settings_page.board_feedback.setText("命令已排队，等待 ACK 和 applied 状态；不代表硬件已执行。")

    def _apply_board_status(self, status: Mic16Status | Mic16RtosStatus) -> None:
        if not self._accept_udp_callback():
            return
        if self._source_mode == "udp":
            self.diagnostics_page.instrument.observer.observe_event("STATUS", detail=type(status).__name__)
        self.link_status.value.setText("收到板端状态")
        if isinstance(status, Mic16RtosStatus):
            mode = ("原始采集", "二维定位")[status.applied_mode]
            pending = status.requested_generation != status.applied_generation
            self.mode_status.value.setText(mode + (" · 切换中" if pending else ""))
            calibration = "标定就绪" if status.calibration_ready else f"标定 {status.calibration_frames}/20"
            self.config_status.value.setText(f"{calibration} · CRC {status.calibration_crc32:08X}")
            fault_names = [name for bit, name in ((1, "DMA"), (2, "定位 PL"), (4, "WDAS")) if status.fault_flags & bit]
            text = (f"故障隔离：{' / '.join(fault_names)}，禁止软件清锁" if fault_names else "未报告隔离故障")
            self.settings_page.board_feedback.setText(
                f"{mode}；请求/已应用 {status.requested_generation}/{status.applied_generation}；{text}；"
                f"原始帧丢弃 {status.raw_frame_drops}，窗口丢弃 {status.window_discards}。"
            )
            self.diagnostics_page.log.appendPlainText(f"[RTOS {status.sequence}] {self.settings_page.board_feedback.text()}")
        else:
            self.diagnostics_page.log.appendPlainText(
                f"[STATUS] mode={status.run_mode} dma={status.dma_ok} udp={status.udp_ok} flags=0x{status.flags:X}"
            )

    def _apply_command_ack(self, ack: Mic16CommandAck) -> None:
        if not self._accept_udp_callback():
            return
        if self._source_mode == "udp":
            self.diagnostics_page.instrument.observer.observe_event("STATUS", detail="COMMAND_ACK（不代表硬件执行）")
        labels = ("已接收", "已接收，等待采集任务应用", "参数错误", "当前固件不支持", "模块已隔离", "过期请求", "报文损坏")
        message = f"命令 0x{ack.command_id:02X}：{labels[ack.result]}；请求/已应用 {ack.requested_generation}/{ack.applied_generation}"
        self.settings_page.board_feedback.setText(message)
        self.diagnostics_page.log.appendPlainText(f"[ACK] {message}")

    def _apply_wdas_diagnostics(self, receipt: WdasDiagnosticsReceipt, *, record_raw=True) -> None:
        if not self._accept_udp_callback():
            return
        if self._source_mode != "udp":
            return
        if (self._udp_worker is not None and
                receipt.listener_session_id != self._udp_worker._listener_session_id):
            return  # 旧接收器排队的回执不属于本会话。
        self._last_diagnostics = receipt
        d = receipt.diagnostics
        self.diagnostics_page.instrument.observer.observe_event("STATUS", d.sequence, "WDAS_DIAGNOSTICS")
        available = "已初始化" if d.quality_ready and d.wdas_ready and not d.fault_flags & 4 else "不可用/未初始化/已隔离"
        self.diagnostics_page.wdas_diagnostics.setText(
            f"WDAS #{d.sequence} · 请求/应用代 {d.requested_generation}/{d.applied_generation} · {available}\n"
            f"质量处理 {d.quality_frames} 帧 / 错误 {d.quality_errors} / 权重回退 {d.quality_fallback_frames}；"
            f"最近成功帧有效对 {d.quality_last_valid_pairs}/32、有效通道 {d.quality_last_active_channels}/16（不是实物健康诊断）\n"
            f"质量最大 {d.quality_max_us / 1000:.3f} ms；单帧质量+WDAS最大 {d.audio_frame_max_us / 1000:.3f} ms；"
            f"超过 {d.audio_frame_budget_us / 1000:.3f} ms 共 {d.audio_frame_over_budget} 帧（不是端到端延迟）\n"
            f"处理端丢弃 {d.enhanced_frame_drops} 帧 / 无有效源 {d.enhanced_no_source_frames} 帧；"
            f"发送成功 {d.enhanced_packets_sent} 包 / 发送失败 {d.enhanced_packet_send_failures} 包（成功不等于 PC 收到）\n"
            "健康权重：单位默认值，实物标定未完成；计数为本次设备运行累计，重启后请重新开始监听。"
        )
        self.diagnostics_page.log.appendPlainText(
            f"[WDAS DIAG {d.sequence}] {receipt.source_endpoint[0]}:{receipt.source_endpoint[1]} · "
            f"quality_errors={d.quality_errors} over_budget={d.audio_frame_over_budget} · {receipt.transport_origin}"
        )
        if record_raw and self._recorder is not None and self._recorder.active:
            try:
                self._recorder.append_diagnostics(receipt)
            except (OSError, ValueError) as exc:
                self.diagnostics_page.log.appendPlainText(f"[DIAGNOSTIC SAVE FAILED] {exc}")
                self.stop_recording(show_message=False)
                self.capture_page.set_recording_state(False, f"诊断保存异常，录制已停止：{exc}")

    def _check_diagnostics_age(self) -> None:
        if self._last_diagnostics is not None and (
                time.monotonic_ns() - self._last_diagnostics.received_monotonic_ns > 3_000_000_000):
            self.diagnostics_page.wdas_diagnostics.setText("WDAS 诊断已过期（超过 3 秒）；请检查连接，不能据此认定当前零故障。")

    def _apply_localization(self, update: LocalizationUpdate) -> None:
        if not self._accept_udp_callback():
            return
        if self._source_mode == "udp":
            self.diagnostics_page.instrument.observer.observe_event("RESULT", update.frame_index, update.algorithm_name)
        self._last_localization = update
        self.overview_page.update_localization(update)
        self.localization_page.update_localization(update)
        self.stream_status.value.setText(
            f"定位 F{update.frame_index:06d} · {len(update.sources)}/{update.candidate_count} 源"
        )
        self.config_status.value.setText(f"CRC 0x{update.calibration_crc32:08X}")
        self.diagnostics_page.log.appendPlainText(
            f"[LOCALIZATION {update.frame_index:06d}] "
            f"sources={len(update.sources)}/{update.candidate_count} "
            f"latency={update.latency_ms:.3f}ms"
        )

    def _apply_enhanced(self, update: EnhancedAudioUpdate) -> None:
        if not self._accept_udp_callback():
            return
        if self._source_mode == "udp":
            self.diagnostics_page.instrument.observer.observe_event("ENHANCED", update.frame_index, "已接受完整增强帧")
        self._last_enhanced = update
        self.enhancement_page.update_enhanced(update)
        self.diagnostics_page.log.appendPlainText(
            f"[WDAS {update.frame_index:06d}] samples={update.samples.size} "
            f"rms={update.output_rms_dbfs:.1f}dBFS"
        )

    def _set_source_identity(self, source: str, mode: str, link: str, detail: str) -> None:
        self.diagnostics_page.instrument.reset(source)
        self._last_diagnostics = None
        self.diagnostics_page.wdas_diagnostics.setText("WDAS 诊断：未知，尚未收到 0x000B 快照（旧固件可能不支持）。")
        self.source_status.value.setText(source)
        self.mode_status.value.setText(mode)
        self.link_status.value.setText(link)
        self.evidence_badge.setText(
            "BOARD LIVE\n协议接收" if source == "BOARD LIVE" else f"{source}\n非板端算法结论"
        )
        self.config_status.value.setText("等待板端 CRC" if source == "BOARD LIVE" else "非板端 / 校准未应用")
        self.diagnostics_page.set_source(source, detail)

    def choose_recording_directory(self) -> None:
        project_root = Path(__file__).resolve().parents[3]
        default_root = project_root / "results" / "gui_recordings"
        default_root.mkdir(parents=True, exist_ok=True)
        selected = QFileDialog.getExistingDirectory(
            self,
            "选择 MIC16 录制保存目录",
            str(default_root),
        )
        if selected:
            self.start_recording(Path(selected))

    def start_recording(self, output_root: Path, *, prefix: str = "") -> bool:
        if self._recorder.busy:
            return False
        if self._source_mode == "synthetic":
            QMessageBox.information(
                self,
                "合成源不作为正式录音",
                "请切换到 Replay 或 BOARD LIVE。Qt 不会把合成波形伪装成真实 raw_i32 录音。",
            )
            return False
        sample_rate = (
            float(self._last_frame.sample_rate_hz)
            if self._last_frame is not None
            else float(OFFICIAL_SAMPLE_RATE)
        )
        try:
            labels = self.capture_page.labels.snapshot()
            session_dir = self._recorder.start(Path(output_root), APP_VERSION,
                evidence_source=self.source_status.value.text(),
                sample_rate_hz=sample_rate,
                prefix=prefix,
                labels=labels,
            )
        except Exception as exc:
            QMessageBox.warning(self, "无法开始录制", str(exc))
            return False
        self._last_recording = None
        if self._source_mode == "udp" and self._udp_worker is not None:
            service, token = self._recorder, str(session_dir)
            self._udp_worker.set_recording_sinks(
                lambda frame: service.append(frame, session_token=token),
                lambda receipt: service.append_diagnostics(receipt, session_token=token))
        self.capture_page.set_recording_state(True, f"后台启动录制 · {session_dir.name}")
        self.diagnostics_page.log.appendPlainText(f"[RECORD START] {session_dir}")
        return True

    def stop_recording(self, *, show_message: bool = True) -> RecordingArtifacts | None:
        if not self._recorder.busy:
            return None
        self._recorder.stop()
        self.capture_page.set_recording_state(False, "正在后台排空队列并保存，请等待完成", busy=True)
        return None

    def _recording_progress(self, progress):
        state = progress["state"]
        if state in ("STARTING", "RECORDING"):
            accepted, written = progress.get("accepted_samples", 0), progress.get("written_samples", 0)
            text = f"录制中 · 已接受 {accepted:,} / 已写入 {written:,} 点 · 队列 {progress.get('queue_items', 0)}/64"
            active = True
        else:
            text = {"STOPPING": "停止接收，后台保存中", "SAVING": "后台生成NPY与哈希清单",
                    "EXPORTING": "后台导出16通道WAV与MAT；界面可继续操作"}.get(state, state)
            active = False
        self.capture_page.set_recording_state(active, text, busy=not active)

    def _recording_finished(self, result):
        artifacts = result["artifacts"]
        if result["action"] == "record":
            self._last_recording = artifacts if result["ok"] else None
        if result["ok"]:
            text = (f"已保存 · {artifacts.sample_count / artifacts.sample_rate_hz:.1f} s · {artifacts.raw_npy.name}"
                    if result["action"] == "record" else "已导出16个WAV与MAT，哈希清单已更新")
        else:
            text = f"未完成：{result['error']}；已生成文件保留，请检查目录"
        self.capture_page.set_recording_state(False, text, has_export=self._last_recording is not None)
        self.diagnostics_page.log.appendPlainText(f"[{result['action'].upper()} {'PASS' if result['ok'] else 'FAILED'}] "
                                                 f"{result['session_dir']} {result['error']}")
        self.statusBar().showMessage(text, 12_000)
        if self._close_pending:
            QTimer.singleShot(0, self.close)

    def export_last_recording(self) -> bool:
        artifacts = self._last_recording
        if artifacts is None:
            QMessageBox.information(self, "没有可导出的录音", "请先完成一次 Replay 或板端 PCM 录制。")
            return False
        try:
            self._recorder.export(artifacts)
        except Exception as exc:
            QMessageBox.warning(self, "WAV/MAT 导出失败", str(exc))
            self.diagnostics_page.log.appendPlainText(f"[EXPORT ERROR] {exc}")
            return False
        self.capture_page.set_recording_state(False, "后台导出中，请等待完成", busy=True)
        return True

    def _finish_recording_for_source_change(self) -> None:
        if self._recorder is not None and self._recorder.active:
            self.stop_recording(show_message=False)

    def _stop_udp(self) -> None:
        self._udp_display_timer.stop()
        worker = self._udp_worker
        thread = self._udp_thread
        self._udp_worker = None
        self._udp_thread = None
        if worker is not None:
            worker.set_recording_sinks()
            worker.stop()
        if thread is not None and thread.isRunning():
            thread.quit()
            thread.wait(1200)

    def toggle_running(self) -> None:
        if self._source_mode == "udp":
            if self._udp_thread is not None and self._udp_thread.isRunning():
                self._stop_udp()
                self._set_running(False)
            else:
                self.start_udp(self._listen_ip, self._port)
            return
        if self._timer.isActive():
            self._timer.stop()
            self._set_running(False)
        else:
            self._timer.start()
            self._set_running(True)

    def _set_running(self, running: bool) -> None:
        if self._source_mode == "udp":
            self.pause_button.setText("停止监听" if running else "继续监听")
            if not running:
                self.link_status.value.setText("UDP 已停止")
            return
        noun = "回放" if self._source_mode == "replay" else "演示"
        self.pause_button.setText(f"暂停{noun}" if running else f"继续{noun}")
        if not running:
            self.link_status.value.setText(f"{noun}已暂停")

    def save_screenshot(self, path: Path) -> bool:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        return self.grab().save(str(path), "PNG")

    def export_evidence_pack(self) -> Path | None:
        if self._last_frame is None and self._last_localization is None and self._last_enhanced is None:
            return None
        project_root = Path(__file__).resolve().parents[3]
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        run_dir = project_root / "results" / "gui_runs" / f"qt_q4b_{timestamp}"
        run_dir.mkdir(parents=True, exist_ok=False)
        screenshot = run_dir / "mic16_qt_q4b_overview.png"
        self.save_screenshot(screenshot)
        manifest = {
            "schema_version": "mic16.gui.session.v1",
            "created_at": datetime.now().isoformat(timespec="seconds"),
            "app_version": APP_VERSION,
            "migration_stage": "Q4-B-PARTIAL",
            "evidence_source": self.source_status.value.text(),
            "board_measurement": self.source_status.value.text() == "BOARD LIVE",
            "pcm_frame_index": self._last_frame.frame_index if self._last_frame is not None else None,
            "localization_frame_index": (
                self._last_localization.frame_index if self._last_localization is not None else None
            ),
            "enhanced_frame_index": (
                self._last_enhanced.frame_index if self._last_enhanced is not None else None
            ),
            "sample_rate_hz": (
                self._last_frame.sample_rate_hz
                if self._last_frame is not None
                else self._last_enhanced.sample_rate_hz if self._last_enhanced is not None else None
            ),
            "algorithm": (
                self._last_localization.algorithm_name
                if self._last_localization is not None
                else self._last_frame.algorithm_name if self._last_frame is not None else None
            ),
            "source_count": (
                len(self._last_localization.sources)
                if self._last_localization is not None
                else len(self._last_frame.sources) if self._last_frame is not None else 0
            ),
            "calibration_crc32": (
                f"0x{self._last_localization.calibration_crc32:08X}"
                if self._last_localization is not None
                else None
            ),
            "files": [screenshot.name],
            "limitations": [
                "Q3 validates versioned PCM/localization/enhancement presentation contracts",
                "Q4-A validates continuous raw PCM recording and NPY/WAV/MAT export contracts",
                "Q4-B adds PC-only instruments and read-only calibration inspection, not calibration application",
                "the board does not transmit a full response surface; accepted markers are shown without a fabricated heatmap",
                "a BOARD LIVE label proves protocol reception only, not acoustic accuracy",
            ],
        }
        (run_dir / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        self.statusBar().showMessage(f"证据包已导出：{run_dir}", 12_000)
        return run_dir

    def closeEvent(self, event: QCloseEvent) -> None:
        if self._recorder.busy:
            self._close_pending = True
            self._timer.stop()
            self._stop_udp()
            self._recorder.stop()
            self.capture_page.set_recording_state(False, "正在后台保存/导出，完成后自动关闭", busy=True)
            event.ignore()
            return
        self.experiment_page.offline.service.shutdown()
        self.experiment_page.frozen.service.shutdown()
        self.diagnostics_page.instrument.refresh_timer.stop()
        self._timer.stop()
        self._diagnostics_timer.stop()
        self._stop_udp()
        event.accept()


def _button(text: str, primary: bool) -> QPushButton:
    button = QPushButton(text)
    button.setObjectName("PrimaryButton" if primary else "SecondaryButton")
    button.setMinimumHeight(36)
    return button
