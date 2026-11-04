"""单页、定位优先的仪器界面，不导入研究页面。保留原接收、录制及协议语义；M16S/M16L作为独立逐源数据流。PCM帧不等于定位结果，本机监听也不等于板卡已经验证连接。"""
from __future__ import annotations

from collections import deque
import csv
from datetime import datetime, timezone
import ipaddress
import json
from pathlib import Path
import time

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import QThread, Qt, QTimer, QRectF,Signal
from PySide6.QtGui import QAction, QCloseEvent, QFont, QPainter
from PySide6.QtWidgets import (
    QAbstractItemView, QComboBox, QDialog, QDialogButtonBox, QFileDialog,
    QFormLayout, QFrame, QGroupBox, QHBoxLayout, QHeaderView, QLabel, QLineEdit,
    QMainWindow, QMenu, QMessageBox, QPlainTextEdit, QPushButton, QSpinBox, QSplitter,
    QTableWidget, QTableWidgetItem, QTabWidget, QToolButton, QVBoxLayout, QWidget,
    QSizePolicy,
)

from mic16_qt.presentation.video_panel import VideoPanel
from mic16_qt import APP_VERSION
from mic16_qt.core.models import AcousticFrame, LocalizationUpdate, SourceEstimate
from mic16_qt.services.audio_sources import ReplayAcousticSource, AudioFrameFactory
from mic16_qt.services.recording_service import RecordingService
from mic16_qt.services.live_observation import LiveObservation
from mic16_qt.services.device_control import DeviceControl
from mic16_qt.services.control_wire import DeviceState, Operation, UNKNOWN
from mic16_qt.services.synthetic_source import SyntheticAcousticSource
from mic16_qt.services.udp_worker import UdpReceiverWorker
from mic16_udp_common import Mic16Status, Mic16RtosStatus, Mic16CommandAck


def configure_plot(plot, bottom, left):
    plot.setBackground('#ffffff')
    plot.setLabel('bottom', bottom, color='#536575')
    plot.setLabel('left', left, color='#536575')
    for name in ('bottom', 'left'):
        plot.getAxis(name).setPen(pg.mkPen('#a5b0ba'))
        plot.getAxis(name).setTextPen(pg.mkPen('#536575'))
    plot.showGrid(x=True, y=True, alpha=.12)
    plot.setMenuEnabled(False)
    plot.hideButtons()


class DirectionView(QWidget):
    def __init__(self):
        super().__init__()
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMinimumSize(240, 160)
        self.plot = pg.PlotWidget(parent=self)
        configure_plot(self.plot, '方位角 / °', '俯仰角 / °')
        self.plot.getPlotItem().layout.setContentsMargins(0, 10, 0, 0)
        self.plot.showGrid(x=False, y=False)
        self.plot.setMouseEnabled(x=False, y=False)
        self.plot.setXRange(-60, 60, padding=0)
        self.plot.setYRange(-30, 30, padding=0)
        self.plot.setLimits(xMin=-180, xMax=180, yMin=-90, yMax=90)
        self.plot.getViewBox().setAspectLocked(True, ratio=1)
        self._fit_timer = QTimer(self)
        self._fit_timer.setSingleShot(True)
        self._fit_timer.timeout.connect(self._fit_plot)
        self.plot.getViewBox().sigResized.connect(self._schedule_plot_fit)
        # 保留蓝—青—黄—红顺序，加深蓝底并增强色彩；不做逐帧自动归一化。
        self.response_colormap = pg.ColorMap([0., .12, .22, .45, .68, .85, 1.],
            [(0,3,46), (0,3,46), (0,28,156), (0,144,208),
             (0,192,40), (246,204,0), (206,0,0)])
        class SmoothResponseImage(pg.ImageItem):
            def paint(self,painter,*args):
                # 仅做一次显示缩放；不对原始响应卷积、锐化或重建峰值。
                painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
                super().paint(painter,*args)
        self.image=SmoothResponseImage(axisOrder='row-major')
        self.image.setLookupTable(self.response_colormap.getLookupTable(nPts=256))
        self.plot.addItem(self.image)
        # 细等值线取自同一原始网格，增强层次可读性，不表示检测边界或定位精度。
        self.contours = []
        for _ in range(3):
            curve = pg.IsocurveItem(pen=pg.mkPen((255,255,255,80), width=.8),
                                    axisOrder='row-major')
            curve.setParentItem(self.image)
            curve.setZValue(1)
            self.contours.append(curve)
        self.response_levels=(.1,.35)
        self.response_auto_scale=False
        self.display_levels=self.response_levels
        self.colorbar=pg.ColorBarItem(values=self.response_levels,colorMap=self.response_colormap,
            label='相对响应',interactive=False, colorMapMenu=False)
        self.colorbar.setImageItem(self.image,insert_in=self.plot.getPlotItem())
        for name in ('left', 'right', 'top', 'bottom'):
            self.colorbar.getAxis(name).setPen(pg.mkPen('#a5b0ba'))
            self.colorbar.getAxis(name).setTextPen(pg.mkPen('#536575'))
        self.colorbar.hide()
        self.markers = pg.ScatterPlotItem(size=5, symbol='o', pen=pg.mkPen('#ffffff',width=1),brush=pg.mkBrush('#f54723'))
        self.markers.setZValue(10)
        self.plot.addItem(self.markers)
        self.labels = []
        self.empty = pg.TextItem('等待声源定位数据', color='#71808e', anchor=(.5, .5))
        self.empty.setPos(0, 0)
        self.plot.addItem(self.empty)
        self._heatmap_key = self._sources_key = None
        self.installation_roll_deg=0.
        self.array_plane_mode=False
        self.point_display_only=False
        self.scan_geometry='LEGACY_RAW_25X13'
        self._angle_limits=(-60.,60.,-30.,30.)
        self.clear()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._schedule_plot_fit()

    def _schedule_plot_fit(self, *args):
        self._fit_timer.start(0)

    def _fit_plot(self):
        # 两轴每度使用相同像素数，避免把0～90度的纵轴拉成两倍高度。
        vb = self.plot.getViewBox()
        rect = vb.boundingRect()
        extra_w = max(0., self.plot.width() - rect.width())
        extra_h = max(0., self.plot.height() - rect.height())
        xmin,xmax,ymin,ymax=self._angle_limits;ratio=(xmax-xmin)/(ymax-ymin)
        map_w = max(1., min(self.width() - extra_w, ratio * (self.height() - extra_h)))
        width = max(1, min(self.width(), round(map_w + extra_w)))
        height = max(1, min(self.height(), round(map_w / ratio + extra_h)))
        self.plot.setGeometry((self.width()-width)//2, (self.height()-height)//2, width, height)
        vb.setRange(xRange=(xmin,xmax), yRange=(ymin,ymax), padding=0)

    def set_installation_roll(self,roll):
        from mic16_av_fusion.installation_coordinates import installed_angles
        installed_angles(0.,0.,roll)
        if self.installation_roll_deg==roll:return
        self.installation_roll_deg=roll
        self._angle_limits=(-60.,60.,-30.,30.) if roll==0 else (-65.,65.,-65.,65.)
        self._schedule_plot_fit()

    def set_array_plane_mode(self,enabled):
        enabled=bool(enabled)
        if self.array_plane_mode==enabled:return
        self.array_plane_mode=enabled
        self._angle_limits=(-60.,60.,-30.,30.) if self.installation_roll_deg==0 else (-65.,65.,-65.,65.)
        self.plot.setLimits(xMin=-180,xMax=180,yMin=-90,yMax=90)
        self.plot.setBackground('#ffffff')
        if enabled:
            self.plot.setLabel('bottom','水平偏角 / °',color='#536575')
            self.plot.setLabel('left','垂直偏角 / °',color='#536575')
            self.plot.getViewBox().setBackgroundColor('#00032e')
            self.plot.setToolTip('矩形图显示声源的水平/垂直偏角，右侧表采用阵列平面球坐标。未扫描区域只显示底色，没有补造响应。')
        else:
            self.plot.setLabel('bottom','方位角 / °',color='#536575');self.plot.setLabel('left','俯仰角 / °',color='#536575')
            self.plot.getViewBox().setBackgroundColor('#ffffff')
        self._schedule_plot_fit()

    def set_point_display_only(self,enabled):
        # 日常定位只画真实方向的小圆点；完整响应仍交给相机和诊断，不伪造扩散图。
        self.point_display_only=bool(enabled)
        self.markers.setSize(9 if enabled else 5)
        if enabled:
            self.plot.getViewBox().setBackgroundColor('#00032e')
            self.image.hide();self.colorbar.hide()
            for curve in self.contours:curve.hide()

    def set_scan_geometry(self,geometry):
        from mic16_av_fusion.hemisphere_grid import HEMISPHERE_GRID,LEGACY_GRID
        if geometry not in (HEMISPHERE_GRID,LEGACY_GRID):raise ValueError('未知扫描网格')
        if self.scan_geometry==geometry:return
        self.scan_geometry=geometry
        wide=geometry==HEMISPHERE_GRID
        self.plot.setLimits(xMin=-180,xMax=360,yMin=-90,yMax=90)
        self.plot.getViewBox().setAspectLocked(True,ratio=1.)
        if wide:
            self._angle_limits=(0.,360.,0.,90.)
            self.plot.setLabel('bottom','方位角 / °',color='#536575')
            self.plot.setLabel('left','俯仰角 / °',color='#536575')
            self.plot.setToolTip('阵列平面球坐标矩形图：MIC12方位0°，MIC0为90°；板面俯仰0°，法线90°。法线处方位未定义。')
        elif self.array_plane_mode:
            self._angle_limits=(-65.,65.,-65.,65.)
            self.plot.setLabel('bottom','水平偏角 / °',color='#536575')
            self.plot.setLabel('left','垂直偏角 / °',color='#536575')
        self._schedule_plot_fit()

    def _update_contours(self, values, levels):
        low, high = levels
        for curve, fraction in zip(self.contours, (.55, .78, .93)):
            # 避免等值线连接跨越NaN未扫描区域，形成并不存在的斜线。
            if self.point_display_only or self.array_plane_mode or not np.isfinite(values).all():curve.hide();continue
            curve.show()
            curve.setData(values, level=low + fraction * (high-low))

    def clear(self, text='等待声源定位数据'):
        self.image.hide()
        self._heatmap_key = self._sources_key = None
        self.colorbar.hide()
        self.set_sources(())
        self.empty.setText(text)
        self.empty.show()

    def set_sources(self, sources):
        if self.array_plane_mode and self.scan_geometry!='ARRAY_HEMISPHERE_25X13':
            from mic16_av_fusion.array_spherical_coordinates import display_view_angles
            positions=[display_view_angles(s.azimuth_deg,s.elevation_deg) for s in sources]
        else:positions=[(s.azimuth_deg,s.elevation_deg) for s in sources]
        self.markers.setData([p[0] for p in positions],[p[1] for p in positions])
        for label in self.labels:
            self.plot.removeItem(label)
        self.labels.clear()
        for s,(x,y) in zip(sources,positions):
            label = pg.TextItem(s.source_id, color='#ffffff', anchor=(0, 1),
                                fill=pg.mkBrush(20,33,52,170))
            font = QFont()
            font.setPixelSize(12)
            label.setFont(font)
            label.setZValue(11)
            offset=1.5
            label.setPos(x+offset,y+offset)
            self.plot.addItem(label)
            self.labels.append(label)
        self.empty.setVisible(not bool(sources))

    def show_result(self, sources, key=None):
        # 只有同epoch、同窗的方向与空间图才叠在一起。
        self._sources_key = key
        self.image.setVisible(not self.point_display_only and key is not None and key == self._heatmap_key and (bool(sources) or not self.array_plane_mode))
        self.colorbar.setVisible(self.image.isVisible())
        self.set_sources(sources)
        self.empty.setText('本次结果：0个声源' if not sources else '')
        if self.image.isVisible():self.empty.hide()

    def show_heatmap(self, frame):
        from mic16_av_fusion.installation_coordinates import installed_response_grid
        from mic16_av_fusion.hemisphere_grid import grid_geometry,HEMISPHERE_GRID
        geometry=grid_geometry(frame);self.set_scan_geometry(geometry)
        if geometry==HEMISPHERE_GRID:
            values,rect,limits=frame.values,(-7.5,-3.75,375.,97.5),(0.,360.,0.,90.)
        else:values,rect,limits=installed_response_grid(frame.values,self.installation_roll_deg)
        self._heatmap_key = frame.key if hasattr(frame,"key") else (frame.epoch, frame.window)
        from mic16_av_fusion.heatmap_rendering import scalar_display_grid,response_display_levels
        self.response_samples=np.array(values,copy=True)
        display_values=scalar_display_grid(values)
        self.display_levels=response_display_levels(values,self.response_levels,automatic=self.response_auto_scale)
        self.image.setImage(display_values, autoLevels=False, levels=self.display_levels)
        self.colorbar.setLevels(self.display_levels);self.colorbar.show()
        # 网格坐标对应像素中心；不把插值后的图像当作更高的阵列分辨率。
        self.image.setRect(QRectF(*rect))
        self._angle_limits=limits
        self._update_contours(display_values, self.display_levels)
        # 无有效方向时保留原数组作诊断，主画面只显示纯深蓝底，避免把噪声扫描轮廓当成声源。
        visible=not self.point_display_only and (not self.array_plane_mode or getattr(frame,'valid',True))
        self.image.setVisible(visible);self.colorbar.setVisible(visible)
        self._schedule_plot_fit()
        if self._sources_key != self._heatmap_key:self.set_sources(())
        self.empty.hide()

    def show_demo(self, frame):
        self.colorbar.hide()
        # 首次绘制时色条也可能回写图像色限，必须同步其内部状态。
        self.colorbar.setLevels((-30, 0))
        self.image.setImage(frame.response_db, autoLevels=False, levels=(-30,0))
        daz = float(frame.azimuth_axis_deg[1]-frame.azimuth_axis_deg[0])
        dele = float(frame.elevation_axis_deg[1]-frame.elevation_axis_deg[0])
        self.image.setRect(QRectF(float(frame.azimuth_axis_deg[0])-daz/2,
            float(frame.elevation_axis_deg[0])-dele/2,
            float(np.ptp(frame.azimuth_axis_deg))+daz,
            float(np.ptp(frame.elevation_axis_deg))+dele))
        self._update_contours(frame.response_db, (-30, 0))
        self.image.setVisible(not self.point_display_only)
        self.set_sources(frame.sources)


class CompactWindow(QMainWindow):
    heatmap_frame_ready=Signal()
    DISPLAY_RESULT_MAX_AGE_S = 5.0  # 这里只控制显示过期，不改变检测器或轨迹记忆规则。

    def __init__(self, *, source_mode='idle', replay_path=None, listen_ip='0.0.0.0', port=5000):
        super().__init__()
        self.setWindowTitle('MIC16 声源定位')
        self.resize(1380, 860)
        self.setMinimumSize(1060, 680)
        self._live_observation=LiveObservation()
        self._mode = 'idle'
        self._active = False
        self._source = None
        self._last_frame = None
        self._last_localization = None
        self._last_result_received = None
        self._last_data_received = None
        self._last_audio_received = None
        self._last_recording = None
        self._history = deque(maxlen=500)
        self._udp_thread = self._udp_worker = None
        self._source_udp_thread = self._source_udp_worker = None
        self._source_preview = None
        self._source_raw_preview = None
        self._heatmap_receiver = self._last_heatmap = None
        self._fast_display_mode=False;self._fast_current=None
        self._refined_records=();self._fast_frame_seen=None
        self._source_port = 5001
        self._source_timeout = 0
        self._source_ready = False
        self._source_error = ''
        self._device_session = False
        self._session_video = self._session_heatmap = False
        self._connection_started = None
        self._start_after_receiver = False
        self._device_control = DeviceControl(self)
        self._device_control.changed.connect(self._device_state_changed)
        self._device_control.error.connect(self._log)
        self._stop_requested = False
        self._rtos_seen = False
        self._close_pending = False
        self._listen_ip, self._port, self._board_ip = str(listen_ip), int(port), '192.168.1.10'
        self._recorder = RecordingService(self)
        self._recorder.progress_changed.connect(self._recording_progress)
        self._recorder.finished.connect(self._recording_finished)
        self._build_ui()
        self.heatmap_frame_ready.connect(self._poll_heatmap,Qt.ConnectionType.QueuedConnection)
        self._timer = QTimer(self)
        self._timer.setInterval(100)
        self._timer.timeout.connect(self._refresh)
        self._timer.start()
        self._refresh_controls()
        if source_mode == 'udp':
            self.start_udp(self._listen_ip, self._port)
        elif source_mode == 'replay' and replay_path is not None:
            self.load_replay(Path(replay_path))
        elif source_mode == 'synthetic':
            self.start_demo()

    def _build_ui(self):
        file_menu = self.menuBar().addMenu('文件')
        self._action(file_menu, '打开音频回放…', self.choose_replay)
        self._action(file_menu, '打开空间响应回放…', self.choose_heatmap_replay)
        self._action(file_menu, '历史定位记录…', self.show_direction_history)
        self._action(file_menu, '打开逐源增强结果…', self.choose_source_audio)
        self._action(file_menu, '连接并录制全部逐源音频…', self.choose_source_recording)
        self._action(file_menu, '处理16通道录音…', self.open_source_processing)
        self._action(file_menu, '保存定位结果 CSV…', self.choose_result_export)
        self._action(file_menu, '导出上次录音 WAV / MAT', self.export_recording)
        file_menu.addSeparator()
        self._action(file_menu, '退出', self.close)
        view_menu = self.menuBar().addMenu('查看')
        self._action(view_menu, '运行日志', self.show_log)
        self._action(view_menu, '摄像头视频', lambda: self.primary_tabs.setCurrentWidget(self.video_panel))
        self._action(view_menu, '演示数据（非实测）', self.start_demo)
        help_menu = self.menuBar().addMenu('帮助')
        self._action(help_menu, '数据与当前连接说明', self.show_data_help)
        self._action(help_menu, '上电后如何使用', self.show_power_on_help)

        root = QWidget()
        root.setObjectName('CompactRoot')
        outer = QVBoxLayout(root)
        outer.setContentsMargins(0,0,0,0)
        outer.setSpacing(4)
        toolbar = QFrame()
        toolbar.setObjectName('CompactToolbar')
        row = QHBoxLayout(toolbar)
        row.setContentsMargins(12,8,12,8)
        title = QLabel('MIC16  声源定位')
        title.setObjectName('CompactTitle')
        row.addWidget(title)
        row.addSpacing(18)
        self.source_combo = QComboBox()
        self.source_combo.addItem('逐源定位 / 增强 UDP', 'source_udp')
        self.source_combo.addItem('原始音频 / 旧协议 UDP', 'udp')
        self.source_combo.addItem('音频回放', 'replay')
        self.source_combo.addItem('演示数据（非实测）', 'synthetic')
        self.source_combo.currentIndexChanged.connect(self._selection_changed)
        row.addWidget(self.source_combo)
        self.connect_button = QPushButton('连接')
        self.connect_button.setObjectName('ConnectButton')
        self.connect_button.clicked.connect(self.toggle_connection)
        row.addWidget(self.connect_button)
        self.settings_button = QPushButton('连接设置')
        self.settings_button.clicked.connect(self.configure_connection)
        row.addWidget(self.settings_button)
        self.start_button = QPushButton('开始定位')
        self.start_button.clicked.connect(self._start_device)
        row.addWidget(self.start_button)
        self.stop_button = QPushButton('停止定位')
        self.stop_button.clicked.connect(self._stop_device)
        row.addWidget(self.stop_button)
        self.heatmap_button = QPushButton('监听热图')
        self.heatmap_button.setToolTip('独立M16H / UDP 5003；需要板端热图候选固件')
        self.heatmap_button.clicked.connect(self.toggle_heatmap)
        row.addWidget(self.heatmap_button)
        row.addStretch(1)
        self.source_badge = QLabel('未连接')
        self.source_badge.setObjectName('SourceBadge')
        row.addWidget(self.source_badge)
        outer.addWidget(toolbar)

        self.connection_caption = QLabel('未连接 · 上电并加载配套固件后，点击连接')
        self.connection_caption.setObjectName('MutedLabel')
        self.connection_caption.setContentsMargins(12, 2, 12, 2)
        self.connection_caption.setWordWrap(True)
        outer.addWidget(self.connection_caption)

        self.notice = QLabel('定位与音频保持独立；可在“摄像头视频”页监听板端 M16V 图像。')
        self.notice.setObjectName('Notice')
        self.notice.setContentsMargins(12,0,12,0)
        self.notice.setWordWrap(True)
        outer.addWidget(self.notice)
        self.main_splitter = QSplitter(Qt.Orientation.Vertical)
        self.main_splitter.setChildrenCollapsible(False)
        primary = QSplitter(Qt.Orientation.Horizontal)
        primary.setChildrenCollapsible(False)
        map_panel = QGroupBox('声源方向')
        map_layout = QVBoxLayout(map_panel)
        self.direction = DirectionView()
        map_layout.addWidget(self.direction, 1)
        self.map_caption = QLabel('仅显示接收的方向；颜色图仅在具有空间响应数据时显示。')
        self.map_caption.setObjectName('MutedLabel')
        display_row=QHBoxLayout()
        display_row.addWidget(self.map_caption,1)
        self.heatmap_scale=QComboBox()
        self.heatmap_scale.addItem('色标 0.10～0.35',(.1,.35))
        self.heatmap_scale.addItem('色标 −0.10～1.00',(-.1,1.))
        self.heatmap_scale.setToolTip('手动调整显示对比度；不修改模型门限、不逐帧归一化')
        self.heatmap_scale.currentIndexChanged.connect(self._set_heatmap_scale)
        display_row.addWidget(self.heatmap_scale)
        map_layout.addLayout(display_row)
        self.continuity_caption=QLabel(self._live_observation.text())
        self.continuity_caption.setObjectName('MutedLabel')
        map_layout.addWidget(self.continuity_caption)
        self.primary_tabs = QTabWidget()
        self.primary_tabs.addTab(map_panel, '定位热图 / 方向')
        self.video_panel = VideoPanel(self, default_addresses=lambda: (self._listen_ip, self._board_ip))
        self.primary_tabs.addTab(self.video_panel, '摄像头视频')
        primary.addWidget(self.primary_tabs)
        side = QGroupBox('声源结果')
        side.setMinimumWidth(260)
        side.setMaximumWidth(390)
        side_layout = QVBoxLayout(side)
        summary = QHBoxLayout()
        self.source_count_label=QLabel('当前源数');summary.addWidget(self.source_count_label)
        self.source_count = QLabel('—')
        self.source_count.setObjectName('CountValue')
        summary.addWidget(self.source_count)
        summary.addStretch(1)
        side_layout.addLayout(summary)
        self.result_state = QLabel('尚无定位结果')
        self.result_state.setObjectName('MutedLabel')
        side_layout.addWidget(self.result_state)
        self.source_table = QTableWidget(0,4)
        self.source_table.setHorizontalHeaderLabels(('编号','方位角','俯仰角','模型分数'))
        self.source_table.verticalHeader().hide()
        self.source_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.source_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.source_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.source_table.setAlternatingRowColors(True)
        side_layout.addWidget(self.source_table,1)
        self.save_result_button = QPushButton('保存定位结果…')
        self.save_result_button.clicked.connect(self.choose_result_export)
        side_layout.addWidget(self.save_result_button)
        self.control_button = QToolButton()
        self.control_button.setText('板端采集控制')
        self.control_button.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        commands = QMenu(self.control_button)
        for label, command, argument in [('读取状态',2,0),('开始采集',4,1),('停止采集',4,0)]:
            action = QAction(label,self)
            action.triggered.connect(lambda checked=False,c=command,a=argument:self.send_board_command(c,a))
            commands.addAction(action)
        self.control_button.setMenu(commands)
        self.control_button.setToolTip('收到兼容的板端RTOS状态后可用；监听UDP不等于启动采集。')
        side_layout.addWidget(self.control_button)
        primary.addWidget(side)
        primary.setSizes([1020,310])
        self.main_splitter.addWidget(primary)

        auxiliary = QWidget()
        aux_layout = QVBoxLayout(auxiliary)
        aux_layout.setContentsMargins(0,0,0,0)
        aux_layout.setSpacing(3)
        aux_header = QHBoxLayout()
        aux_header.addWidget(QLabel('音频监视'))
        self.channel_combo = QComboBox()
        self.channel_combo.addItems([f'CH{i+1:02d}' for i in range(16)])
        self.channel_combo.currentIndexChanged.connect(self._redraw_audio)
        aux_header.addWidget(self.channel_combo)
        self.source_kind_combo=QComboBox()
        self.source_kind_combo.addItem('神经增强','enhanced');self.source_kind_combo.addItem('WDAS','WDAS')
        self.source_kind_combo.currentIndexChanged.connect(self._redraw_audio)
        self.source_kind_combo.hide();aux_header.addWidget(self.source_kind_combo)
        self.audio_state = QLabel('尚无音频数据')
        self.audio_state.setObjectName('MutedLabel')
        aux_header.addWidget(self.audio_state)
        aux_header.addStretch(1)
        self.record_button = QPushButton('开始录制…')
        self.record_button.clicked.connect(self.choose_recording)
        self.stop_record_button = QPushButton('停止并保存')
        self.stop_record_button.clicked.connect(self.stop_recording)
        aux_header.addWidget(self.record_button)
        aux_header.addWidget(self.stop_record_button)
        aux_layout.addLayout(aux_header)
        self.audio_tabs = QTabWidget()
        self.wave_plot = pg.PlotWidget()
        configure_plot(self.wave_plot,'本帧时间 / ms','PCM 数值')
        self.wave_curve = self.wave_plot.plot(pen=pg.mkPen('#27669b',width=1))
        self.spectrum_plot = pg.PlotWidget()
        configure_plot(self.spectrum_plot,'频率 / Hz','相对峰值 / dB')
        self.spectrum_plot.setYRange(-80,3,padding=0)
        self.spectrum_curve = self.spectrum_plot.plot(pen=pg.mkPen('#27669b',width=1))
        self.audio_tabs.addTab(self.wave_plot,'波形')
        self.audio_tabs.addTab(self.spectrum_plot,'频谱')
        aux_layout.addWidget(self.audio_tabs,1)
        auxiliary.setMinimumHeight(150)
        self.main_splitter.addWidget(auxiliary)
        self.main_splitter.setSizes([550,200])
        self.main_splitter.setStretchFactor(0,4)
        self.main_splitter.setStretchFactor(1,1)
        outer.addWidget(self.main_splitter,1)
        self.record_state = QLabel('未录制')
        self.record_state.setObjectName('MutedLabel')
        self.record_state.setContentsMargins(12,0,12,4)
        outer.addWidget(self.record_state)
        self.setCentralWidget(root)
        self._log_messages = deque(maxlen=400)
        self.statusBar().showMessage('未连接 · 不自动监听或加载演示数据')

    def _action(self, menu, title, callback):
        action = QAction(title,self)
        action.triggered.connect(callback)
        menu.addAction(action)
        return action

    def _log(self, text):
        self._log_messages.append(str(text))

    def _selection_changed(self):
        if not self.disconnect_source():
            return
        self._refresh_controls()

    def _select(self, mode):
        self.source_combo.blockSignals(True)
        self.source_combo.setCurrentIndex(self.source_combo.findData(mode))
        self.source_combo.blockSignals(False)

    def _refresh_controls(self):
        busy = self._recorder.busy
        self.source_combo.setEnabled(not busy)
        self.settings_button.setEnabled(not self._active and not busy)
        self.connect_button.setEnabled(not busy)
        noun = {'udp':'连接','source_udp':'连接','replay':'打开回放…','synthetic':'启动演示'}[self.source_combo.currentData()]
        self.connect_button.setText(('取消连接' if self._mode=='source_udp' and not self._source_ready else '断开') if self._active else noun)
        rearmable = self._source_udp_thread is not None and not self._source_udp_thread.isRunning()
        self.start_button.setEnabled(self._device_session and (self._source_ready or rearmable)
                                    and not self._source_error and not self._start_after_receiver
                                    and self._device_control.can_start)
        self.stop_button.setEnabled(self._device_control.can_stop)
        single=self._device_control.status is not None and bool(self._device_control.status.capabilities & 64)
        self.start_button.setText('单次定位诊断' if single else '开始定位')
        self.start_button.setToolTip('采满一组完整输入后定位并自动待机；这是单次诊断，非持续实时' if single else '收到兼容常驻固件的待机确认后可开始；不自动采集背景或重新下载')
        self.stop_button.setToolTip('由板端确认停止与排空；点击成功发送不代表已停止')
        raw_ready = self._last_frame is not None and self._last_frame.raw_pcm_i32 is not None
        if self._mode=='udp':
            raw_ready = raw_ready and self._last_audio_received is not None and time.monotonic()-self._last_audio_received<2.0
        self.record_button.setEnabled(self._active and raw_ready and self._mode in ('udp','replay') and not busy)
        self.stop_record_button.setEnabled(self._recorder.active)
        self.save_result_button.setEnabled(bool(self._history))
        self.control_button.setEnabled(self._active and self._mode=='udp' and self._rtos_seen)
        self.control_button.setVisible(self.source_combo.currentData()=='udp')
        compatible=self._active and self._mode=='udp' and self._rtos_seen
        self.control_button.setText('板端采集控制' if compatible else '板端开始/停止（未接通）')
        if not compatible:self.control_button.setToolTip('当前音视频固件尚未接通常驻开始/停止；连接只打开接收，不会下载或启动板卡。')

    def _clear_views(self):
        self._fast_display_mode=False;self._fast_current=None;self._fast_frame_seen=None;self._refined_records=()
        self.source_count_label.setText('当前源数');self.source_table.setColumnHidden(3,False)
        self._live_observation.reset()
        self._last_frame = self._last_localization = None
        self._last_data_received = self._last_result_received = None
        self._last_audio_received = None
        self.video_panel.set_acoustic_sources((),window=None,origin='DISCONNECTED')
        self.source_count.setText('—')
        self.result_state.setText('尚无定位结果')
        self.source_table.setRowCount(0)
        self.direction.clear()
        self.wave_curve.setData([],[])
        self.spectrum_curve.setData([],[])
        self.audio_state.setText('尚无音频数据')
        self.map_caption.setText('仅显示接收的方向；颜色图仅在具有空间响应数据时显示。')
        self._rtos_seen = False

    def disconnect_source(self):
        if self._recorder.busy:
            self.notice.setText('录制或保存尚未完成，请先停止并保存。')
            return False
        self._device_control.close()
        self._device_session = False
        self._start_after_receiver = False
        self._source_ready = False
        pending_receivers = False
        if self._session_video:
            if not self.video_panel.shutdown():
                pending_receivers = True
            else:
                self._session_video = False
        if self._session_heatmap:
            if not self.stop_heatmap():
                pending_receivers = True
            else:
                self._session_heatmap = False
        self._active = False
        self._source = None
        if self._source_udp_worker is not None:
            self._source_udp_worker.stop()
            if not self._source_udp_thread.wait(1500):
                self.notice.setText('逐源会话正在保存，请稍候。');return False
            self._source_udp_thread.deleteLater()
            self._source_udp_worker=self._source_udp_thread=None
        self._source_preview=None;self._source_raw_preview=None
        self.source_kind_combo.hide()
        self.channel_combo.blockSignals(True);self.channel_combo.clear()
        self.channel_combo.addItems([f'CH{i+1:02d}' for i in range(16)]);self.channel_combo.blockSignals(False)
        if self._udp_worker is not None and not self._stop_requested:
            self._udp_worker.set_recording_sinks()
            self._udp_worker.stop()
            self._stop_requested = True
        if self._udp_thread is not None:
            self._udp_thread.quit()
            if not self._udp_thread.wait(1500):
                self.notice.setText('正在等待接收线程退出，请稍后再操作。')
                return False
            self._udp_thread.deleteLater()
            self._udp_thread = self._udp_worker = None
        self._stop_requested = False
        # 界面会话及时结束；仍在退出的接收器由后续重试逐一确认。
        self._mode = 'idle'
        self._source_error = ''
        self._connection_started = None
        self._clear_views()
        self._badge('未连接')
        if pending_receivers:
            # 同一轮先通知所有接收器停止；不能因第一路尚未退出而漏停后续线程。
            QTimer.singleShot(50, self.disconnect_source)
            self.notice.setText('正在关闭设备接收…')
            self._refresh_controls()
            return False
        self.notice.setText('已断开；方向图已清空，仍可保存此前的定位结果。')
        self.statusBar().showMessage('已断开')
        self.connection_caption.setText('接收已断开 · 如需停止板端运行，请先点击“停止定位”并等待确认')
        self._refresh_controls()
        return True

    def _badge(self, label, demo=False):
        self.source_badge.setText(label)
        self.source_badge.setProperty('demo',demo)
        self.source_badge.style().unpolish(self.source_badge)
        self.source_badge.style().polish(self.source_badge)

    def toggle_connection(self):
        if self._active:
            self.disconnect_source()
        elif self.source_combo.currentData()=='udp':
            self.start_udp(self._listen_ip,self._port)
        elif self.source_combo.currentData()=='source_udp':
            self.connect_device()
        elif self.source_combo.currentData()=='replay':
            self.choose_replay()
        else:
            self.start_demo()

    def connect_device(self):
        """日常一次连接定位、视频和空间响应；控制握手不能代替数据到达。"""
        for receiver, port, label in ((self.video_panel.receiver, 5002, '视频'),
                                      (self._heatmap_receiver, 5003, '热图')):
            if receiver is not None and (receiver.listen_ip, receiver.port, receiver.board_ip) != (self._listen_ip, port, self._board_ip):
                self.notice.setText(f'已有{label}会话的IP或端口与当前设备不同，请先停止该会话。')
                self.connection_caption.setText(self.notice.text())
                return False
        self.connection_caption.setText('正在连接设备…')
        if not self.start_source_udp(self._listen_ip, self._source_port, self._board_ip):
            self.connection_caption.setText(self.notice.text())
            return False
        self._device_session = True
        self._connection_started = time.monotonic()
        try:
            if self.video_panel.video_backend is None:
                # 本地USB未选择或未到货不能阻止板卡定位连接。
                try:
                    self.video_panel.start_selected_video()
                    self._session_video = True
                except (ValueError, OSError, RuntimeError) as video_error:
                    self.video_panel.state.setText('视频未打开：' + str(video_error))
            if self._heatmap_receiver is None:
                self.start_heatmap(self._listen_ip, 5003, self._board_ip)
                self._session_heatmap = True
            self.video_panel.overlay_enabled.setChecked(True)
            self._device_control.open(self._listen_ip, self._board_ip)
        except (ValueError, OSError, RuntimeError) as exc:
            self._source_error = self._connection_error(exc)
            self.connection_caption.setText('设备连接未完成：' + self._source_error)
            self.notice.setText(self.connection_caption.text())
            self._log(self.notice.text())
            self._refresh_controls()
            return False
        self.notice.setText('定位和热图接收已启动，等待板端确认；USB视频可独立打开，再点击“开始定位”。')
        self._device_state_changed()
        return True

    @staticmethod
    def _connection_error(error):
        text = str(error)
        if '10048' in text or 'Address already in use' in text:
            return '接收端口已被占用，请关闭另一个接收程序后重试。' + text
        if '10049' in text or 'Cannot assign requested address' in text:
            return '本机没有所选监听IP，请在连接设置中使用0.0.0.0。' + text
        return text

    def _source_udp_event(self, event_id, kind, text):
        worker = self._source_udp_worker
        if worker is None or event_id != worker.event_id or self._mode != 'source_udp':
            return
        if kind == 'error':
            self._source_udp_error(text)
        elif kind == 'ready' and self._active:
            self._source_udp_state(text)

    def _source_udp_state(self, text):
        self._source_ready = True
        self.statusBar().showMessage(text)
        if self._start_after_receiver:
            self._start_after_receiver = False
            self._start_device()
        self._device_state_changed()

    def _device_state_changed(self):
        self._refresh_controls()
        if not self._device_session:
            return
        parts = ['定位接收' + ('就绪' if self._source_ready else '正在打开')]
        receiver = self.video_panel.video_backend
        video = receiver.snapshot() if receiver else {}
        parts.append('视频' + ('已到达' if video.get('completed', 0) else '等待画面' if receiver else '未打开'))
        parts.append('定位' + ('已到达' if self._last_result_received is not None else '等待结果'))
        parts.append(self._device_control.message)
        status = self._device_control.status
        self._fast_display_mode=status is not None and bool(status.capabilities & 128)
        single=status is not None and bool(status.capabilities & 64)
        if self._source_udp_worker is not None and status is not None:
            self._source_udp_worker.set_localization_only(bool(status.capabilities & 16))
        if self._fast_display_mode and status.epoch and status.state in (DeviceState.STARTING,DeviceState.RUNNING,DeviceState.DRAINING):
            self._bind_display_epoch(status.boot,status.epoch)
        if status is not None:
            if status.compute_us != UNKNOWN:
                parts.append(f'板端计算{status.compute_us/1000.:.1f}ms')
            if status.queued_windows != UNKNOWN:
                parts.append(f'待处理{status.queued_windows}窗')
        if self._source_error:
            parts.append('接收错误：' + self._source_error)
        self.connection_caption.setText(' · '.join(parts))
        # 文案以板端新回复为准，收到FAULT/RUNNING后不能保留旧的开始等待提示。
        pending = self._device_control.pending
        if status is None or not self._device_control.responsive:
            self.notice.setText('控制状态未确认：' + self._device_control.message)
        elif status.state == DeviceState.FAULT:
            self.notice.setText('定位时段已中止：' + self._device_control.message + '；恢复板端后再开始。')
            self._badge('板端故障 · 本时段已中止')
        elif pending is not None and pending['operation'] in (Operation.START, Operation.STOP):
            self.notice.setText('等待板端确认' + ('开始定位。' if pending['operation'] == Operation.START else '停止并排空。'))
        elif status.state == DeviceState.STARTING:
            self.notice.setText('板端已确认开始，正在准备采集；等待首个定位结果。')
            self._badge('板端正在启动')
        elif status.state == DeviceState.RUNNING:
            self.notice.setText('单次诊断正在采集；采满后定位，非持续实时。' if single else '板端正在持续处理；收到新定位后自动刷新，未收到时不填入测试点。')
            self._badge('板端正在运行')
        elif status.state == DeviceState.DRAINING:
            self.notice.setText('单次诊断正在计算和发送结果，完成后自动待机。' if single else '板端正在停止并排空，期间继续接收末尾数据。')
            self._badge('板端正在排空')
        elif self._device_control.can_start:
            self.notice.setText('单次诊断已就绪，点击“单次定位诊断”；本模式非持续实时。' if single else '板端已就绪，点击“开始定位”后持续运行。')
            self._badge('板端已就绪')
        else:
            self.notice.setText(self._device_control.message)


    def _start_device(self):
        if not self._device_control.can_start:
            self.notice.setText('尚不能开始：需要板端常驻固件就绪，且没有待确认命令。')
            return
        if not self._source_ready:
            # 前一次STOP的最终音频窗会结束接收段；重新开始须使用新组装器。
            old_thread = self._source_udp_thread
            if old_thread is not None and old_thread.isRunning():
                self.notice.setText('上一段接收尚未结束，请等待停止完成。')
                return
            if old_thread is not None:
                old_thread.deleteLater()
            self._source_udp_worker = self._source_udp_thread = None
            self._start_after_receiver = True
            if not self._open_source_udp(self._listen_ip, self._source_port, self._board_ip):
                self._start_after_receiver = False
            self._refresh_controls()
            return
        status=self._device_control.status
        if status is not None and status.capabilities & 16 and self._source_udp_worker is not None:
            if status.capabilities & 128:self._bind_display_epoch(status.boot,status.epoch+1)
            else:self._source_udp_worker.begin_localization_epoch(status.epoch+1)
        if self._device_control.start():
            self._last_result_received=None;self._last_heatmap=None
            self.direction.clear('正在采集并计算新结果');self.source_table.setRowCount(0);self.source_count.setText('—')
            self.notice.setText('单次诊断请求已发送，等待板端确认。' if status.capabilities & 64 else '开始请求已发送，等待板端确认；确认后声源结果会持续更新。')
        else:
            self.notice.setText('尚不能开始：需要板端常驻固件就绪，且定位接收成功。')
        self._device_state_changed()

    def _stop_device(self):
        if self._device_control.stop():
            self.notice.setText('停止请求已发送，等待板端排空；期间保留接收。')
        self._device_state_changed()

    def choose_source_recording(self):
        root=QFileDialog.getExistingDirectory(self,'选择定位及网络音频保存目录（新会话）')
        if root:self.start_source_udp(self._listen_ip,self._source_port,self._board_ip,root)

    def show_power_on_help(self):
        path=Path(__file__).resolve().parents[4]/'docs/power_on_software_guide.md'
        dialog=QDialog(self);dialog.setWindowTitle('上电后如何使用');dialog.resize(780,650)
        layout=QVBoxLayout(dialog);view=QPlainTextEdit();view.setReadOnly(True)
        view.setPlainText(path.read_text(encoding='utf-8') if path.is_file() else '操作说明未找到：'+str(path))
        layout.addWidget(view);buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        buttons.rejected.connect(dialog.reject);layout.addWidget(buttons);dialog.exec()

    def start_udp(self, listen_ip, port):
        try:
            if ipaddress.ip_address(listen_ip).version!=4 or not 1<=int(port)<=65535:
                raise ValueError('需要有效IPv4地址和1—65535端口')
        except ValueError as exc:
            self.notice.setText(str(exc))
            return False
        if not self.disconnect_source():
            return False
        self._listen_ip,self._port = str(listen_ip),int(port)
        self._history.clear()
        self._select('udp')
        self._mode,self._active = 'udp',True
        worker = UdpReceiverWorker(self._listen_ip,self._port,frame_samples=512,coalesce_display=True)
        thread = QThread(self)
        worker.moveToThread(thread)
        thread.started.connect(worker.run)
        worker.state_changed.connect(self._on_udp_state)
        worker.receive_error.connect(self._on_udp_error)
        worker.finished.connect(thread.quit,Qt.ConnectionType.DirectConnection)
        thread.finished.connect(worker.deleteLater)
        self._udp_thread,self._udp_worker = thread,worker
        self._badge('等待板端数据')
        self.notice.setText('UDP监听不等于已收到数据；请确认固件提供音频或定位数据流。')
        self._refresh_controls()
        thread.start()
        return True

    def _on_udp_state(self, text):
        if self.sender() is self._udp_worker and self._active:
            self.statusBar().showMessage(text+' · 等待数据')

    def start_source_udp(self,listen_ip,port,board_ip,output_root=None,**options):
        if not self.disconnect_source():return False
        return self._open_source_udp(listen_ip,port,board_ip,output_root,**options)

    def _open_source_udp(self,listen_ip,port,board_ip,output_root=None,**options):
        from mic16_qt.services.source_network import SourceNetworkWorker
        options.setdefault('idle_timeout_s',self._source_timeout)
        try:worker=SourceNetworkWorker(listen_ip,port,board_ip,output_root,raw_port=5005,**options)
        except (ValueError,OSError) as exc:
            self.notice.setText('逐源监听无法启动：'+str(exc));return False
        self._listen_ip,self._source_port,self._board_ip=str(listen_ip),int(port),str(board_ip)
        self._select('source_udp');self._mode,self._active='source_udp',True;self._history.clear()
        self._source_ready = False
        self._source_error = ''
        self._connection_started = time.monotonic()
        thread=QThread(self);worker.moveToThread(thread);thread.started.connect(worker.run)
        worker.lifecycle_event.connect(self._source_udp_event)
        worker.fast_frame_ready.connect(self._fast_packet_arrived,Qt.ConnectionType.QueuedConnection)
        worker.finished.connect(thread.quit,Qt.ConnectionType.DirectConnection)
        thread.finished.connect(worker.deleteLater)
        self._source_udp_thread,self._source_udp_worker=thread,worker
        self.source_kind_combo.show();self.channel_combo.clear()
        self._badge('正在打开定位接收…')
        self.notice.setText(('所有源独立保存；不自动播放。' if output_root is not None else '仅预览，不自动录制或播放。')+'连接仅打开接收；板端须已运行对应固件。')
        self._refresh_controls();thread.start();return True

    def _fast_packet_arrived(self,event_id):
        worker=self._source_udp_worker
        if worker is None or event_id!=worker.event_id or not self._active or self._mode!='source_udp':return
        self._refresh_source_udp()
        self._poll_heatmap()

    def _source_udp_error(self,text):
        self._source_ready = False
        self._source_error = self._connection_error(text)
        self._start_after_receiver = False
        self.notice.setText('定位接收失败：' + self._source_error)
        self._badge('连接失败')
        self.connection_caption.setText(self.notice.text())
        self._log(text)
        self._refresh_controls()

    def _bind_display_epoch(self,boot,epoch):
        if self._source_udp_worker is not None:self._source_udp_worker.begin_display_epoch(boot,epoch)
        if self._heatmap_receiver is not None:self._heatmap_receiver.begin_display_epoch(boot,epoch)
        identity=(boot,epoch)
        if self._fast_frame_seen is None or self._fast_frame_seen[:2]!=identity:
            self._fast_current=None;self._fast_frame_seen=(*identity,-1)
            self._last_heatmap=None;self._refined_records=();self.direction.clear('等待新定位')
            self.video_panel.clear_spatial_response()

    def _apply_fast_direction(self,frame):
        if self._fast_frame_seen is not None and (frame.epoch_key!=self._fast_frame_seen[:2] or frame.sequence<=self._fast_frame_seen[2]):return False
        self._fast_current=frame;self._fast_frame_seen=(*frame.epoch_key,frame.sequence)
        sources=(SourceEstimate('S1',frame.azimuth_deg,frame.elevation_deg,0.,'CURRENT','主导方向；质量来自空间响应'),) if frame.valid else ()
        self.direction.show_result(sources,frame.key);self._show_sources(sources,frame.sequence)
        # 一个主导方向不承担全场声源总数估计。
        self.source_count_label.setText('当前方向');self.source_count.setText('1' if frame.valid else '—')
        self.source_table.setColumnHidden(3,True)
        self.result_state.setText('方向已更新' if frame.valid else '暂无有效方向')
        self._last_result_received=frame.received_monotonic_s;self._last_data_received=frame.received_monotonic_s
        self._live_observation.observe(frame.epoch,frame.sequence)
        self._history.append(dict(received_utc=datetime.now(timezone.utc).isoformat(),frame_index=frame.sequence,
            evidence_source='UDP_RECEIVED_UNVERIFIED',algorithm='A2_H4_PHYSICAL_RESPONSE',sources=sources,
            boot=frame.boot,epoch=frame.epoch,first_sample=frame.first_sample,end_sample=frame.end_sample,
            completed_tick=frame.completed_tick,tick_hz=frame.tick_hz,quality=(frame.coherence,frame.contrast,frame.support_fraction)))
        if not frame.valid:self.video_panel.clear_spatial_response()
        else:self.video_panel.expect_spatial_response(frame)
        self._apply_fast_map()
        return True

    def _apply_fast_map(self):
        from mic16_av_fusion.fast_display_wire import same_observation
        if (same_observation(self._fast_current,self._last_heatmap) and
            0<=time.monotonic()-self._fast_current.received_monotonic_s<1. and
            0<=time.monotonic()-self._last_heatmap.received_monotonic_s<1.):
            self.direction.show_heatmap(self._last_heatmap)
            self.direction.show_result((SourceEstimate('S1',self._fast_current.azimuth_deg,self._fast_current.elevation_deg,0.,'CURRENT'),) if self._fast_current.valid else (),self._fast_current.key)
            self.video_panel.set_spatial_response(self._fast_current,self._last_heatmap,self.direction.response_levels)

    def make_direction_history_dialog(self):
        dialog=QDialog(self);dialog.setWindowTitle('历史定位记录');dialog.resize(800,640);layout=QVBoxLayout(dialog)
        view=DirectionView();view.markers.setBrush(pg.mkBrush('#6551b8'));view.markers.setPen(pg.mkPen('#dfd8ff'))
        layout.addWidget(view,1);dialog.history_view=view
        table=QTableWidget(0,5);table.setHorizontalHeaderLabels(['样本区间','编号','方位角','俯仰角','模型分数'])
        historical_sources=[]
        for event in self._refined_records:
            for source in event['sources']:
                row=table.rowCount();table.insertRow(row)
                historical_sources.append(SourceEstimate(f"ID{source['source_id']}",source['azimuth_deg'],source['elevation_deg'],0.,'HISTORY',score=source['score']))
                texts=[f"{event['input_start_sample']}—{event['input_end_sample_exclusive']}",str(source['source_id']),f"{source['azimuth_deg']:+.1f}°",f"{source['elevation_deg']:+.1f}°",f"{source['score']:.4f}"]
                for col,text in enumerate(texts):
                    item=QTableWidgetItem(text);item.setForeground(pg.mkColor('#6551b8'));table.setItem(row,col,item)
        table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers);table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        def selected():
            row=table.currentRow()
            if 0<=row<len(historical_sources):view.show_result((historical_sources[row],))
        table.itemSelectionChanged.connect(selected)
        if historical_sources:table.selectRow(len(historical_sources)-1);selected()
        layout.addWidget(table,1);return dialog

    def show_direction_history(self):self.make_direction_history_dialog().exec()

    def _refresh_source_udp(self):
        worker=self._source_udp_worker
        if worker is None:return
        latest=worker.take_snapshot()
        if 'fast_directions' in latest:self._apply_fast_direction(latest['fast_directions'])
        if 'refined_history' in latest:self._refined_records=latest['refined_history']
        if 'display_error' in latest:self._log(latest['display_error'])
        if ('directions' in latest and not self._fast_display_mode and
            (not self._device_session or self._device_control.status is not None)):
            event=latest['directions']
            sources=tuple(SourceEstimate(f"ID{s['source_id']}",s['azimuth_deg'],s['elevation_deg'],0.,
                'CURRENT',f"模型分数{s['score']:.4f}；非声压级",score=s['score']) for s in event['sources'])
            self.direction.show_result(sources,(event['epoch'],event['window']));self._show_sources(sources,event['window'])
            self._remember_result(sources,event['window'],'UDP_RECEIVED_UNVERIFIED','FROZEN_C16_M16L')
            self._live_observation.observe(event['epoch'],event['window'])
            self._last_data_received = time.monotonic()
            self.video_panel.set_acoustic_sources(sources,window=event['window'],origin=worker.evidence_source)
            self.map_caption.setText('冻结C16方向；仅与同窗空间响应叠加，未标定声像坐标')
            self._badge('本机环回测试 · 非实测' if worker.evidence_source=='LOCAL_UDP_TEST' else '收到逐源协议（未验收）')
        elif not self._history and self._source_ready:
            self._badge('接收就绪 · 等待板端')
        if 'raw_error' in latest:
            self.record_state.setText('原始PCM不完整；定位继续：'+latest['raw_error'])
            self._log(latest['raw_error'])
        if 'raw_pcm' in latest:
            raw=latest['raw_pcm'];self._source_raw_preview=raw
            if self.source_kind_combo.currentData()!='RAW':
                self.channel_combo.blockSignals(True);self.channel_combo.clear()
                for channel in range(16):self.channel_combo.addItem(f'CH{channel+1:02}',channel)
                self.channel_combo.blockSignals(False)
                self.source_kind_combo.blockSignals(True);self.source_kind_combo.clear()
                self.source_kind_combo.addItem('原始PCM','RAW');self.source_kind_combo.setEnabled(False)
                self.source_kind_combo.blockSignals(False)
            self._redraw_source_audio()
            self.record_state.setText(f"16路原始PCM：帧{raw['frame']}，已接收{raw['completed']}帧；未做板端增强")
        if 'audio' in latest:
            event=latest['audio'];self._source_preview=event
            identity=self.channel_combo.currentData();self.channel_combo.blockSignals(True);self.channel_combo.clear()
            for sid,s in event['tracks'].items():
                self.channel_combo.addItem(f"ID{sid}"+('' if s['current_confirmed'] else '（边界音频）'),sid)
            index=self.channel_combo.findData(identity)
            self.channel_combo.setCurrentIndex(max(0,index));self.channel_combo.blockSignals(False)
            self._redraw_source_audio()
            self.record_state.setText(f"逐源已保存 {event['end']:,} 点 · {worker.directory}" if worker.directory else "仅预览 · 未录制")
        if not self._source_udp_thread.isRunning():
            self._active=False
            self._source_ready=False
            if worker.result:
                if worker.directory is None:
                    self._badge('预览已停止' if not worker.result.get('error') else '连接失败')
                    self.notice.setText('预览已停止；没有自动保存音频。'+worker.result.get('error',''))
                    self._refresh_controls();return
                if worker.result.get('status')=='STOPPED_LOCALIZATION_ONLY':
                    self._badge('仅定位接收已停止')
                    self._source_audio_directory=str(worker.directory)
                    self.statusBar().showMessage('定位与原始音频保存目录：'+str(worker.directory))
                    self.notice.setText('仅定位会话记录已保留，未运行板端增强；未作实板性能验收。')
                    if worker.evidence_source=='LOCAL_UDP_TEST':self.notice.setText('本机UDP环回测试。'+self.notice.text())
                    self._refresh_controls();return
                ok=worker.result.get('status')=='COMPLETED' 
                self._badge('逐源会话已保存' if ok else '逐源会话未完成')
                self._source_audio_directory=str(worker.directory)
                self.statusBar().showMessage(('逐源已保存：' if ok else '逐源未完成：')+str(worker.directory))
                self.notice.setText(('逐源完整保存，可从“文件→打开逐源增强结果”试听。' if ok else
                    '本段未完成，保留诊断文件；重新连接会创建新会话。')+' 未作实板性能验收。')
                if worker.evidence_source=='LOCAL_UDP_TEST':
                    self.notice.setText('本机UDP环回测试，非声学实测。'+self.notice.text())
            self._refresh_controls()

    def _redraw_source_audio(self):
        raw=self._source_raw_preview
        if raw is not None and self.source_kind_combo.currentData()=='RAW':
            channel=self.channel_combo.currentData()
            if channel is None:return
            values=raw['pcm'][:,int(channel)];rate=78125/2
            self.wave_curve.setData((np.arange(len(values))+raw['first_sample'])/rate,values)
            self.wave_plot.setLabel('bottom','输入样本相对时间 / s')
            self.wave_plot.setLabel('left','原始PCM幅值（非SPL）')
            self.spectrum_curve.setData(np.fft.rfftfreq(len(values),1/rate),AudioFrameFactory._spectrum_db(np.asarray(values,float)*np.hanning(len(values))))
            self.spectrum_plot.setXRange(0,rate/2,padding=0)
            self.audio_state.setText('39.0625 kHz · 16路原始PCM · 未增强')
            return
        if self._source_preview is None:return
        source=self._source_preview['tracks'].get(self.channel_combo.currentData())
        if source is None:
            self.wave_curve.clear();self.spectrum_curve.clear();self.audio_state.setText('本窗没有逐源音频');return
        values=source[self.source_kind_combo.currentData()];end=self._source_preview['end']
        self.wave_curve.setData(np.arange(end-len(values),end)/16000.,values)
        self.wave_plot.setLabel('bottom','会话相对时间 / s');self.wave_plot.setLabel('left','归一化幅值（非SPL）')
        self.wave_plot.getAxis('left').enableAutoSIPrefix(False)
        self.spectrum_curve.setData(np.fft.rfftfreq(len(values),1/16000.),AudioFrameFactory._spectrum_db(np.asarray(values,float)*np.hanning(len(values))))
        self.spectrum_plot.setXRange(0,8000,padding=0)
        self.audio_state.setText('16 kHz · '+('所有源后台保存' if self._source_udp_worker and self._source_udp_worker.directory else '仅预览，未录制')+' · 选择仅影响预览')

    def _on_udp_error(self, text):
        if self.sender() is self._udp_worker and self._mode=='udp':
            self.notice.setText('接收错误：'+text)
            self._log(text)

    def load_replay(self, path):
        if self._recorder.busy:
            return False
        try:
            source = ReplayAcousticSource(Path(path),frame_samples=512)
        except Exception as exc:
            self.notice.setText('回放打开失败：'+str(exc))
            return False
        if not self.disconnect_source():
            return False
        self._select('replay')
        self._history.clear()
        self._mode,self._active,self._source = 'replay',True,source
        self._badge('音频回放 · REPLAY')
        self.notice.setText('只回放原始音频；未运行定位算法，不生成声源点。')
        self.map_caption.setText('音频文件不包含定位结果，当前方向图留空。')
        self.statusBar().showMessage('音频回放中 · '+source.path.name)
        self._refresh_controls()
        return True

    def choose_replay(self):
        if self._recorder.busy:
            return
        path,_ = QFileDialog.getOpenFileName(self,'打开16通道原始录音','','NumPy原始录音 (*.npy)')
        if path:
            self.load_replay(Path(path))

    def choose_source_audio(self):
        initial = getattr(self, '_source_audio_directory', '')
        if not initial:
            root = Path(__file__).resolve().parents[3]
            try:
                config = json.loads((root / 'config/project_paths.json').read_text(encoding='utf-8-sig'))
                relative = config['required_enhancement_delivery']['latest_PC_session_directory']
                suggested = (root / relative).resolve()
                if suggested.is_relative_to(root) and suggested.is_dir():
                    initial = str(suggested)
            except (OSError, ValueError, KeyError, TypeError):
                pass
        path = QFileDialog.getExistingDirectory(self, '选择包含 session.json 的逐源结果目录', initial)
        if path:
            self.open_source_audio(path)

    def open_source_processing(self):
        from mic16_qt.presentation.source_processing_dialog import SourceProcessingDialog
        for child in self.findChildren(SourceProcessingDialog):
            child.show()
            child.raise_()
            return child
        dialog = SourceProcessingDialog(self)
        dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        dialog.show()
        return dialog

    def open_source_audio(self, path):
        # 延迟导入；离线试听不初始化模型或接收器。
        from mic16_qt.services.source_audio_session import SourceAudioSession
        from mic16_qt.presentation.source_audio_dialog import SourceAudioDialog
        try:
            session = SourceAudioSession.load(path)
            dialog = SourceAudioDialog(session, self)
        except (OSError, ValueError, KeyError, TypeError) as error:
            QMessageBox.warning(self, '无法打开逐源结果', str(error))
            return None
        dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self._source_audio_directory = str(session.directory)
        dialog.show()
        return dialog

    def start_demo(self):
        if not self.disconnect_source():
            return False
        self._select('synthetic')
        self._history.clear()
        self._mode,self._active,self._source = 'synthetic',True,SyntheticAcousticSource()
        self._badge('演示数据 · 非实测',demo=True)
        self.notice.setText('当前为确定性界面演示，不是板卡数据或算法性能实验。')
        self.map_caption.setText('演示空间响应 · 归一化证据，非声压级')
        self.statusBar().showMessage('演示运行中 · SYNTHETIC · 非实测')
        self._refresh_controls()
        return True

    def choose_heatmap_replay(self):
        path,_=QFileDialog.getOpenFileName(self,'打开空间响应回放','','M16H 空间响应 (*.m16h)')
        if path:
            try:self.load_heatmap_replay(path)
            except (ValueError,OSError) as exc:self.notice.setText('热图回放未打开：'+str(exc))

    def load_heatmap_replay(self,path):
        import struct
        from mic16_av_fusion.heatmap_wire import HeatmapAssembler
        path=Path(path)
        if path.stat().st_size>4096:raise ValueError('单窗热图回放超过大小上限')
        data=path.read_bytes();position=0;assembler=HeatmapAssembler();frame=None
        while position<len(data):
            if len(data)-position<4:raise ValueError('回放长度字段截断')
            size=struct.unpack_from('<I',data,position)[0];position+=4
            if size>1032 or size>len(data)-position:raise ValueError('回放分片截断')
            value=assembler.push(data[position:position+size],('OFFLINE_REPLAY',0));position+=size
            if value is not None:
                if frame is not None:raise ValueError('文件包含多个完整窗')
                frame=value
        if frame is None or assembler.pending:raise ValueError('空间响应回放不完整')
        if not self.disconnect_source() or not self.stop_heatmap():raise ValueError('请等待旧会话关闭后再打开回放')
        self._mode='heatmap_replay';self._last_heatmap=frame;self.direction.show_heatmap(frame)
        self._badge('空间响应回放 · 非实测')
        self.notice.setText('离线静态窗回放；不代表连续声源移动、板端实时或实际定位精度。')
        self.map_caption.setText(f'回放：{path.name} · 窗{frame.window} · 非实时 / 非SPL')
        self.primary_tabs.setCurrentIndex(0)
        return frame

    def _set_heatmap_scale(self):
        selection=self.heatmap_scale.currentData()
        self.direction.response_auto_scale=selection is None
        self.direction.response_levels=(.1,.35) if selection is None else tuple(selection)
        if self._fast_display_mode:self._apply_fast_map()
        elif self._heatmap_is_current():self.direction.show_heatmap(self._last_heatmap)

    def toggle_heatmap(self):
        if self._heatmap_receiver is None:
            try:self.start_heatmap()
            except (ValueError, OSError, RuntimeError) as exc:self.notice.setText('热图监听失败：'+str(exc))
        else:self.stop_heatmap()

    def start_heatmap(self, listen_ip=None, port=5003, board_ip=None):
        from mic16_av_fusion.heatmap_receiver import HeatmapReceiver
        if self._heatmap_receiver is not None:raise RuntimeError('请先停止旧热图监听')
        if self._mode=='heatmap_replay':self._mode='idle'
        receiver=HeatmapReceiver(listen_ip or self._listen_ip,port,board_ip or self._board_ip,
                                 notify_ready=self.heatmap_frame_ready.emit)
        bound=receiver.start();self._heatmap_receiver=receiver;self._last_heatmap=None
        self.heatmap_button.setText('停止热图')
        self.map_caption.setText(f'监听空间响应 UDP {bound} · 等待完整数据窗')
        self.notice.setText('热图来自实际空间扫描；颜色为相对响应，声源数仍由冻结模型判定。')
        return bound

    def stop_heatmap(self):
        receiver=self._heatmap_receiver
        if receiver is not None and not receiver.close(0):
            QTimer.singleShot(100,self.stop_heatmap);return False
        self._heatmap_receiver=self._last_heatmap=None
        self.heatmap_button.setText('监听热图');self.direction.clear('热图已停止')
        return True

    def _single_diagnostic(self):
        status=self._device_control.status
        return self._mode=='source_udp' and status is not None and bool(status.capabilities & 64)

    def _heatmap_is_current(self, now=None):
        now=time.monotonic() if now is None else now
        return self._last_heatmap is not None and (self._mode=='heatmap_replay' or self._single_diagnostic() or now-self._last_heatmap.received_monotonic_s<5.)

    def _poll_heatmap(self):
        receiver=self._heatmap_receiver
        if receiver is None:return
        frame=receiver.take_latest()
        if frame is not None:
            if self._fast_display_mode:
                if hasattr(frame,'boot'):
                    self._last_heatmap=frame;self._apply_fast_map()
            else:self._last_heatmap=frame;self.direction.show_heatmap(frame)
        if self._last_heatmap is not None:
            frame=self._last_heatmap;age=time.monotonic()-frame.received_monotonic_s
            self.map_caption.setText(f'空间响应 · 窗{frame.window} · 接收后{age:.1f}s（非SPL）')
            if age>=(1. if self._fast_display_mode else 5.) and not self._single_diagnostic():
                self.direction.image.hide();self.direction.colorbar.hide();self.direction._heatmap_key=None
                self.direction.empty.setText('热图已过期，等待新数据');self.direction.empty.show()
        stats=receiver.snapshot()
        if stats['error']:self.notice.setText('热图接收错误：'+stats['error'])

    def _fast_display_caption(self, now=None):
        """运行状态与结果新鲜度共同决定文案，人工停止不冒充断流。"""
        now = time.monotonic() if now is None else now
        control = self._device_control
        status = control.status
        if not self._device_session:
            return '定位接收已断开'
        if status is None or not control.responsive:
            return '等待板端状态确认'
        if status.state == DeviceState.IDLE:
            return '定位已停止 · 板端已就绪' if status.epoch else '板端已就绪 · 点击开始定位'
        if status.state == DeviceState.DRAINING:
            return '正在停止并排空'
        if status.state == DeviceState.FAULT:
            return '定位已停止 · 板端故障'
        if status.state == DeviceState.STARTING:
            return '正在准备定位'
        current = self._fast_current
        if current is not None and 0 <= now-current.received_monotonic_s < 1.:
            return '定位正在更新' if current.valid else '定位正在更新 · 暂无有效方向'
        return '等待新的定位数据'

    def _refresh(self):
        self.continuity_caption.setText(self._fast_display_caption() if self._fast_display_mode else self._live_observation.text())
        if self._fast_display_mode:self.continuity_caption.setToolTip(self._live_observation.text())
        if self._device_session:
            self._device_state_changed()
        self._poll_heatmap()
        if self._active and self._mode=='source_udp':
            self._refresh_source_udp()
        elif self._active and self._mode=='udp' and self._udp_worker is not None:
            events,counts = self._udp_worker.take_display_snapshot()
            for event in events:
                if isinstance(event,AcousticFrame): self._apply_audio(event,record=False)
                elif isinstance(event,LocalizationUpdate): self._apply_localization(event)
                elif isinstance(event,(Mic16Status,Mic16RtosStatus)):
                    self._received_data()
                    self._rtos_seen = isinstance(event,Mic16RtosStatus)
                    self._refresh_controls()
                elif isinstance(event,Mic16CommandAck):
                    self.notice.setText(f'收到命令确认（结果码{event.result}）；不等于硬件已经执行。')
            self.wave_plot.setToolTip(f"绘图省略{counts['display_replaced_pcm_frames']}帧；原始录制独立于绘图。")
            if not self._udp_thread.isRunning():
                self._active = False
                self._badge('监听已停止')
                self._refresh_controls()
        elif self._active and self._source is not None:
            try:
                self._apply_audio(self._source.next_frame())
            except Exception as exc:
                self._active = False
                self.notice.setText('数据源错误：'+str(exc))
                self._refresh_controls()
        self.expire_results()
        if self._mode=='udp' and self._active:
            now = time.monotonic()
            if self._last_data_received is not None and now-self._last_data_received>=5.0:
                self._badge('数据已超时')
            if self._last_audio_received is not None and now-self._last_audio_received>=2.0:
                self.audio_state.setText('音频已停止更新 · 保留末帧供查看')
                self._refresh_controls()

    def _received_data(self):
        self._last_data_received = time.monotonic()
        if self._mode=='udp':
            self._badge('收到板端协议数据')

    def _apply_audio(self, frame: AcousticFrame, *, record=True):
        frame.validate()
        self._last_frame = frame
        self._last_audio_received = time.monotonic()
        self._received_data()
        if record and self._recorder.active:
            self._recorder.append(frame)
        self._redraw_audio()
        if self._mode=='synthetic' and frame.evidence_source=='SYNTHETIC':
            self.direction.show_demo(frame)
            self._show_sources(frame.sources,frame.frame_index)
            self._remember_result(frame.sources,frame.frame_index,'SYNTHETIC',frame.algorithm_name)
        self._refresh_controls()

    def _redraw_audio(self):
        if self._mode=='source_udp':
            self._redraw_source_audio();return
        frame = self._last_frame
        if frame is None:
            return
        ch = self.channel_combo.currentIndex()
        raw = frame.raw_pcm_i32
        values = frame.pcm[:,ch] if raw is None else raw[:,ch]
        x = np.arange(values.size)*1000.0/frame.sample_rate_hz
        self.wave_curve.setData(x,values)
        self.wave_plot.setLabel('bottom','本帧时间 / ms')
        self.wave_plot.setLabel('left','PCM 整数' if raw is not None else '演示归一化幅值')
        signal = np.asarray(values,dtype=np.float64)
        spectrum = AudioFrameFactory._spectrum_db(signal*np.hanning(len(signal)))
        frequency = np.fft.rfftfreq(len(signal),1.0/frame.sample_rate_hz)
        self.spectrum_curve.setData(frequency,spectrum)
        self.spectrum_plot.setXRange(0,min(8000,frame.sample_rate_hz/2),padding=0)
        self.audio_state.setText(f'{frame.sample_rate_hz:g} Hz · 16通道 · 帧{frame.frame_index} · 丢包{frame.packet_loss}')

    def _show_sources(self, sources, frame_index):
        self.source_count.setText(str(len(sources)))
        self.result_state.setText(f'结果帧 {frame_index}')
        self.source_table.setRowCount(len(sources))
        for row,s in enumerate(sources):
            for column,text in enumerate([s.source_id,f'{s.azimuth_deg:+.1f}°',f'{s.elevation_deg:+.1f}°',f'{s.score:.3f}' if getattr(s,'score',None) is not None else '—']):
                item = QTableWidgetItem(text)
                item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                item.setToolTip(s.evidence_text or s.state)
                self.source_table.setItem(row,column,item)

    def _remember_result(self, sources, frame, origin, algorithm):
        self._last_result_received = time.monotonic()
        self._history.append(dict(received_utc=datetime.now(timezone.utc).isoformat(),
            frame_index=frame,evidence_source=origin,algorithm=algorithm,sources=tuple(sources)))

    def _apply_localization(self, update: LocalizationUpdate):
        if self._mode!='udp' or not self._active:
            return
        update.validate()
        self._last_localization = update
        self._received_data()
        self.direction.show_result(update.sources)
        self._show_sources(update.sources,update.frame_index)
        self._remember_result(update.sources,update.frame_index,update.evidence_source,update.algorithm_name)
        self.video_panel.set_acoustic_sources(update.sources,window=update.frame_index,origin=update.evidence_source)
        self.map_caption.setText('接收的声源方向 · 协议未提供空间响应图，不补造热力图。')
        self.notice.setText(f'已收到定位结果 · {update.algorithm_name}')
        self._refresh_controls()

    def expire_results(self, now=None):
        now = time.monotonic() if now is None else now
        if self._fast_display_mode:
            status = self._device_control.status
            idle = status is not None and status.state == DeviceState.IDLE
            stale = self._fast_current is not None and not 0<=now-self._fast_current.received_monotonic_s<1.
            if idle or stale:
                caption = '定位已停止，点击开始定位' if idle else '等待新定位'
                self.direction.clear(caption);self.source_table.setRowCount(0);self.source_count.setText('—')
                self.result_state.setText('定位已停止' if idle else '定位数据已中断')
                self.video_panel.clear_spatial_response()
            return
        if self._single_diagnostic() and self._last_result_received is not None:
            self.result_state.setText(f'上次单次结果 · 接收后{max(0.,now-self._last_result_received):.1f}s · 非实时')
            return
        if self._last_result_received is not None and now-self._last_result_received>=self.DISPLAY_RESULT_MAX_AGE_S:
            if self._heatmap_is_current(now):
                self.direction.set_sources(());self.direction.empty.hide()
            else:self.direction.clear('定位结果已过期，等待更新')
            self.source_table.setRowCount(0)
            self.source_count.setText('—')
            self.result_state.setText('结果已过期')
            self._last_result_received = None

    def choose_result_export(self):
        if not self._history:
            self.notice.setText('尚无可保存的定位结果。')
            return
        path,_ = QFileDialog.getSaveFileName(self,'保存最近500次定位结果','mic16_localization.csv','CSV (*.csv)')
        if path:
            try:
                self.export_results(Path(path))
            except OSError as exc:
                self.notice.setText('定位CSV保存失败：'+str(exc))
                self._log(str(exc))

    def export_results(self,path):
        if not self._history:
            return False
        with Path(path).open('w',newline='',encoding='utf-8-sig') as stream:
            writer = csv.writer(stream)
            writer.writerow(['received_utc','frame_index','evidence_source','algorithm','source_count','source_id','azimuth_deg','elevation_deg',
                             'coordinate_frame','installation_roll_deg','raw_azimuth_deg','raw_elevation_deg',
                             'angle_reference_plane','installation_user_confirmed','installation_pose_measured','azimuth_defined'])
            for row in self._history:
                shown=row.get('installed_sources',row['sources']);raw={s.source_id:s for s in row['sources']}
                for source in shown or (None,):
                    original=raw.get(source.source_id) if source else None
                    writer.writerow([row['received_utc'],row['frame_index'],row['evidence_source'],row['algorithm'],len(shown),
                        source.source_id if source else '',source.azimuth_deg if source and row.get('azimuth_defined',True) else '',source.elevation_deg if source else '',
                        row.get('coordinate_frame','INSTALLED_ARRAY') if 'installed_sources' in row else 'ARRAY_RAW',row.get('installation_roll_deg',''),
                        original.azimuth_deg if original else '',original.elevation_deg if original else '',
                        row.get('angle_reference_plane',''),row.get('installation_user_confirmed',''),
                        row.get('installation_pose_measured',''),row.get('azimuth_defined','')])
        self.notice.setText('已保存最近定位结果：'+str(path))
        return True

    def choose_recording(self):
        root = QFileDialog.getExistingDirectory(self,'选择录音保存目录')
        if root:
            self.start_recording(Path(root))

    def start_recording(self, output_root, *, prefix=''):
        frame = self._last_frame
        if (not self._active or self._mode not in ('udp','replay') or frame is None or
                frame.raw_pcm_i32 is None or self._recorder.busy or
                (self._mode=='udp' and (self._last_audio_received is None or
                                       time.monotonic()-self._last_audio_received>=2.0))):
            self.notice.setText('需要先收到原始音频；演示数据和只有定位的会话不可录制PCM。')
            return False
        try:
            session = self._recorder.start(output_root,APP_VERSION,evidence_source=frame.evidence_source,
                sample_rate_hz=frame.sample_rate_hz,prefix=prefix)
        except Exception as exc:
            self.notice.setText('录制无法开始：'+str(exc))
            return False
        self._last_recording = None
        if self._mode=='udp' and self._udp_worker is not None:
            service,token = self._recorder,str(session)
            self._udp_worker.set_recording_sinks(
                lambda f:service.append(f,session_token=token),
                lambda d:service.append_diagnostics(d,session_token=token))
        self.record_state.setText('录制中 · '+str(session))
        self._refresh_controls()
        return True

    def stop_recording(self):
        if self._udp_worker is not None and not self._stop_requested:
            self._udp_worker.set_recording_sinks()
        self._recorder.stop()
        self.record_state.setText('后台保存中…')
        self._refresh_controls()

    def _recording_progress(self, progress):
        self.record_state.setText(f"{progress['state']} · 已写入{progress.get('written_samples',0):,}点")
        self._refresh_controls()

    def _recording_finished(self, result):
        if result['action']=='record':
            self._last_recording = result['artifacts'] if result['ok'] else None
        self.record_state.setText(('已保存：' if result['ok'] else '未完成，请检查日志：')+result['session_dir'])
        self._log(str({k:v for k,v in result.items() if k!='artifacts'}))
        self._refresh_controls()
        if self._close_pending:
            QTimer.singleShot(0,self.close)

    def export_recording(self):
        if self._last_recording is None:
            self.notice.setText('请先完成一次原始音频录制。')
            return
        try:
            self._recorder.export(self._last_recording)
            self.record_state.setText('后台导出WAV / MAT…')
        except Exception as exc:
            self.notice.setText('导出失败：'+str(exc))
        self._refresh_controls()

    def configure_connection(self):
        dialog = QDialog(self)
        dialog.setWindowTitle('连接设置')
        form = QFormLayout(dialog)
        local,board = QLineEdit(self._listen_ip),QLineEdit(self._board_ip)
        source_stream=self.source_combo.currentData()=='source_udp'
        port = QSpinBox();port.setRange(1,65535);port.setValue(self._source_port if source_stream else self._port)
        form.addRow('本机监听IPv4',local)
        form.addRow('板卡控制IPv4',board)
        form.addRow('UDP端口',port)
        timeout=QSpinBox();timeout.setRange(0,120);timeout.setSpecialValueText('保持监听');timeout.setValue(self._source_timeout)
        if source_stream:form.addRow('无数据超时 / 秒',timeout)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok|QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(dialog.accept);buttons.rejected.connect(dialog.reject)
        form.addRow(buttons)
        if dialog.exec()!=QDialog.DialogCode.Accepted:
            return
        try:
            if any(ipaddress.ip_address(v.text()).version!=4 for v in (local,board)):
                raise ValueError('需要IPv4地址')
        except ValueError as exc:
            self.notice.setText('设置未保存：'+str(exc));return
        if source_stream and port.value()==5000:
            self.notice.setText('逐源流需要独立端口，默认5001；旧协议继续使用5000。');return
        self._listen_ip,self._board_ip = local.text(),board.text()
        if source_stream:
            self._source_port=port.value();self._source_timeout=timeout.value()
        else:self._port=port.value()
        self.statusBar().showMessage(f'监听设置 {self._listen_ip}:{port.value()}')

    def send_board_command(self, command, argument):
        if not (self._mode=='udp' and self._active and self._rtos_seen and self._udp_worker):
            self.notice.setText('尚未收到兼容的RTOS状态，不能发送采集控制。')
            return False
        try:
            if not self._udp_worker.send_command(self._board_ip,self._port,command,argument):
                raise ValueError('发送队列已满')
        except ValueError as exc:
            self.notice.setText(str(exc));return False
        self.notice.setText('命令已排队；等待ACK及板端实际状态。')
        return True

    def show_log(self):
        dialog = QDialog(self);dialog.setWindowTitle('运行日志');dialog.resize(720,400)
        layout = QVBoxLayout(dialog)
        view = QPlainTextEdit('\n'.join(self._log_messages));view.setReadOnly(True);layout.addWidget(view)
        dialog.exec()

    def show_data_help(self):
        QMessageBox.information(self,'数据说明',
            '本界面保留定位、逐源增强试听、音频监视/录制和独立视频接收。\n'
            '板端UDP：等待兼容协议的数据，不在PC伪造定位结果。\n'
            '音频回放：只显示音频，不自动运行定位模型。\n'
            '演示数据：明确标为非实测，不允许正式PCM录制。\n\n'
            '视频页监听独立M16V流；保留下载包尚未接入发送器，仍可通过HDMI查看。\n视频尚未与声音核验同步，不自动叠加声源点。\n'
            '方向结果5秒无更新则从显示区清除，CSV保留接收时间；这不是物理采集时间标定。')

    def save_screenshot(self,path):
        Path(path).parent.mkdir(parents=True,exist_ok=True)
        return self.grab().save(str(path),'PNG')

    def closeEvent(self,event: QCloseEvent):
        self._active = False
        processing = [d for d in self.findChildren(QDialog) if getattr(d, 'processing_busy', False)]
        if processing:
            for dialog in processing:
                dialog.close()
            QTimer.singleShot(100,self.close)
            event.ignore()
            return
        if self._recorder.busy:
            self._close_pending = True
            self.stop_recording()
            event.ignore()
            return
        if not self.disconnect_source():
            self._close_pending = True
            QTimer.singleShot(300,self.close)
            event.ignore()
            return
        if not self.video_panel.shutdown():
            QTimer.singleShot(100,self.close)
            event.ignore()
            return
        if not self.stop_heatmap():
            QTimer.singleShot(100,self.close);event.ignore();return
        self._timer.stop()
        for dialog in self.findChildren(QDialog):
            dialog.close()
        event.accept()
