"""逐源离线试听；选择声源不启动推理。"""
from __future__ import annotations

from PySide6.QtCore import Qt, QUrl
from PySide6.QtMultimedia import QMediaPlayer
from PySide6.QtWidgets import (
    QComboBox, QDialog, QHBoxLayout, QLabel, QPushButton, QSlider, QVBoxLayout,
)
import pyqtgraph as pg

from mic16_qt.services.source_audio_session import SourceAudioSession
from mic16_qt.services.async_audio_player import AsyncAudioPlayer


class SourceAudioDialog(QDialog):
    def __init__(self, session: SourceAudioSession, parent=None):
        super().__init__(parent)
        self.session = session
        self.setWindowTitle('MIC16 逐源增强结果 · 离线试听')
        self.resize(1000, 570)
        layout = QVBoxLayout(self)
        network=session.evidence_source in ('UDP_RECEIVED_UNVERIFIED','LOCAL_UDP_TEST')
        label = ('网络接收录音（未作实板验收）' if network else
                 ('仿真回放' if session.evidence_source == 'SIMULATED_REPLAY' else '录音离线回放'))
        notice = QLabel(f'{label} · {session.segment_id} · {session.output_samples / 16000:.3f} 秒\n'
                        '各源已分别重建并保存；此处只切换试听，不改变其它源。非板端实时结果。')
        notice.setWordWrap(True)
        layout.addWidget(notice)
        row = QHBoxLayout()
        row.addWidget(QLabel('试听源'))
        self.source_combo = QComboBox()
        for sid in session.source_ids:
            self.source_combo.addItem(f'源 ID {sid}', sid)
        row.addWidget(self.source_combo)
        self.kind_combo = QComboBox()
        for name, kind in [('神经增强', 'enhanced'), ('WDAS 波束输出', 'WDAS')]+([] if network else [('混合输入 MIC0', 'input_MIC0')]):
            self.kind_combo.addItem(name, kind)
        row.addWidget(self.kind_combo)
        row.addStretch()
        layout.addLayout(row)
        self.details = QLabel()
        self.details.setWordWrap(True)
        layout.addWidget(self.details)
        self.plot = pg.PlotWidget()
        self.plot.setBackground('white')
        self.plot.setLabel('bottom', '会话相对时间 / s')
        self.plot.setLabel('left', '幅值（未逐路归一化）')
        self.plot.getAxis('left').enableAutoSIPrefix(False)
        self.plot.showGrid(x=True, y=True, alpha=.12)
        self.plot.setMenuEnabled(False)
        self.curve = self.plot.plot(pen=pg.mkPen('#22618c', width=1))
        layout.addWidget(self.plot, 1)
        self.seek = QSlider(Qt.Orientation.Horizontal)
        self.seek.setRange(0, max(0, round(session.output_samples / 16) - 1))
        layout.addWidget(self.seek)
        controls = QHBoxLayout()
        self.play_button = QPushButton('播放')
        self.pause_button = QPushButton('暂停')
        self.stop_button = QPushButton('停止')
        self.time_label = QLabel()
        for widget in (self.play_button, self.pause_button, self.stop_button, self.time_label):
            controls.addWidget(widget)
        controls.addStretch()
        controls.addWidget(QLabel('音量'))
        volume = QSlider(Qt.Orientation.Horizontal)
        volume.setRange(0, 100)
        volume.setValue(25)
        volume.setMaximumWidth(140)
        controls.addWidget(volume)
        layout.addLayout(controls)
        self.status = QLabel('未播放；需点击“播放”。')
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        footer = QLabel('ID 是本段内的空间轨迹编号，不是说话人身份。未活动时补零仅用于时间对齐；\n'
                        '切换源后从首次确认附近预览。波形每次只读最多 2 秒；试听保留文件原幅值。')
        footer.setWordWrap(True)
        layout.addWidget(footer)
        self.player = AsyncAudioPlayer(self)
        self._pending_seek_ms = None
        self.player.positionChanged.connect(self._position_changed)
        self.player.mediaStatusChanged.connect(self._media_status)
        self.player.errorOccurred.connect(self._media_error)
        self.player.readyChanged.connect(lambda ready:self.status.setText('试听设备已就绪。'))
        self.play_button.clicked.connect(self._play)
        self.pause_button.clicked.connect(self.player.pause)
        self.stop_button.clicked.connect(self.player.stop)
        self.seek.sliderReleased.connect(self._seek)
        volume.valueChanged.connect(lambda value: self.player.setVolume(value / 100))
        self.source_combo.currentIndexChanged.connect(self._selection_changed)
        self.kind_combo.currentIndexChanged.connect(self._selection_changed)
        self._selection_changed()

    def _selection_changed(self):
        self.player.stop()
        kind = self.kind_combo.currentData()
        sid = None if kind == 'input_MIC0' else self.source_combo.currentData()
        self.source_combo.setEnabled(kind != 'input_MIC0' and bool(self.session.source_ids))
        self.asset = self.session.assets.get((sid, kind))
        self.play_button.setEnabled(self.asset is not None)
        if self.asset is None:
            self.curve.clear()
            self.details.setText('本段没有已保存的逐源音频；可选择混合输入。')
            self.player.setSource(QUrl())
            return
        start = self.session.first_confirmation.get(sid, 0)
        self._pending_seek_ms = round(start / 16)
        self.details.setText(f'{self.asset.path.name} · 16 kHz · {self.asset.frames:,} 点')
        self.details.setToolTip(str(self.asset.path))
        self.player.setSource(QUrl.fromLocalFile(str(self.asset.path)))
        self.seek.setValue(self._pending_seek_ms or 0)
        self.status.setText('已选择；后台准备试听设备，波形可直接查看。' if not self.player.ready else '已选择；未自动播放。')
        self._preview(start)
        self._time_text(self.seek.value())

    def _preview(self, start_sample):
        if self.asset is None:
            return
        x, y = self.asset.preview(start_sample, 16000)
        self.curve.setData(x, y)
        self.plot.enableAutoRange()

    def _time_text(self, ms):
        self.time_label.setText(f'{ms / 1000:.2f} / {self.session.output_samples / 16000:.2f} s')

    def _position_changed(self, ms):
        if self._pending_seek_ms is not None:
            return
        if not self.seek.isSliderDown():
            self.seek.setValue(ms)
        self._time_text(ms)

    def _media_status(self, status):
        if status in (QMediaPlayer.MediaStatus.LoadedMedia,QMediaPlayer.MediaStatus.BufferedMedia) and self._pending_seek_ms is not None:
            target = self._pending_seek_ms
            self._pending_seek_ms = None
            self.player.setPosition(target)
            self.seek.setValue(target)
            self._time_text(target)

    def _seek(self):
        ms = self.seek.value()
        self._pending_seek_ms = None
        self.player.setPosition(ms)
        self._preview(ms * 16)
        self._time_text(ms)

    def _play(self):
        self._pending_seek_ms = None
        self.player.setPosition(self.seek.value())
        self.player.play()
        self.status.setText('请求播放当前文件；如无声音，请检查输出设备和音量。')

    def _media_error(self, error, message):
        self.status.setText('播放失败：' + message)

    def closeEvent(self, event):
        self.player.shutdown()
        super().closeEvent(event)
