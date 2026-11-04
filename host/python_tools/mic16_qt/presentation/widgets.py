from __future__ import annotations

from collections import deque

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QFont
from PySide6.QtWidgets import (
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QSizePolicy,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from mic16_qt.core.models import AcousticFrame, SourceEstimate
from mic16_qt.presentation.theme import COLORS


class MetricCard(QFrame):
    def __init__(self, label: str, value: str = "--", detail: str = "", parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("MetricCard")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 11, 14, 11)
        layout.setSpacing(3)
        title = QLabel(label)
        title.setObjectName("MetricLabel")
        self.value_label = QLabel(value)
        self.value_label.setObjectName("MetricValue")
        self.detail_label = QLabel(detail)
        self.detail_label.setObjectName("MetricLabel")
        layout.addWidget(title)
        layout.addWidget(self.value_label)
        layout.addWidget(self.detail_label)

    def set_value(self, value: str, detail: str | None = None) -> None:
        self.value_label.setText(value)
        if detail is not None:
            self.detail_label.setText(detail)


class SectionFrame(QFrame):
    def __init__(self, title: str, meta: str = "", parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("Panel")
        outer = QVBoxLayout(self)
        outer.setContentsMargins(12, 10, 12, 12)
        outer.setSpacing(8)
        header = QHBoxLayout()
        label = QLabel(title)
        label.setObjectName("SectionTitle")
        header.addWidget(label)
        header.addStretch(1)
        self.meta_label = QLabel(meta)
        self.meta_label.setObjectName("SectionMeta")
        header.addWidget(self.meta_label)
        outer.addLayout(header)
        self.body = QVBoxLayout()
        self.body.setContentsMargins(0, 0, 0, 0)
        outer.addLayout(self.body, 1)


class WaveformView(QWidget):
    def __init__(self, channels: tuple[int, ...] = (0, 4, 8, 12), history_samples: int = 4096) -> None:
        super().__init__()
        self.channels = channels
        self.history_samples = history_samples
        self.sample_rate_hz = 16_000
        self.buffer = np.zeros((history_samples, 16), dtype=np.float32)
        self._last_identity = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.plot = pg.PlotWidget()
        _configure_plot(self.plot, "时间 / ms", "通道（归一化偏移）")
        self.plot.setYRange(-1.0, len(channels) * 2.25, padding=0.03)
        self.plot.showGrid(x=True, y=True, alpha=0.13)
        palette = ("#38C6E5", "#F0B44D", "#6ED39A", "#D889D8", "#7EA7FF", "#FF8F78")
        self.curves = [
            self.plot.plot(pen=pg.mkPen(palette[index % len(palette)], width=1), antialias=False)
            for index, _channel in enumerate(channels)
        ]
        for index, channel in enumerate(channels):
            text = pg.TextItem(f"CH{channel + 1:02d}", color="#BFD0DC", anchor=(0, 0.5))
            text.setPos(-252.0, index * 2.0)
            self.plot.addItem(text)
        layout.addWidget(self.plot)

    def update_frame(self, frame: AcousticFrame) -> None:
        chunk = frame.pcm
        identity = (frame.evidence_source, frame.sample_rate_hz, frame.packet_loss, frame.frame_index)
        if (self._last_identity is not None and
                (identity[:3] != self._last_identity[:3] or identity[3] != self._last_identity[3] + 1)):
            # 合并后的显示帧不一定是相邻PCM，不能跨省略帧或
            # 数据源边界虚构连续时间轴。
            self.buffer.fill(np.nan)
        self._last_identity = identity
        size = min(chunk.shape[0], self.history_samples)
        self.buffer = np.roll(self.buffer, -size, axis=0)
        self.buffer[-size:, :] = chunk[-size:, :]
        self.sample_rate_hz = frame.sample_rate_hz
        x_ms = (np.arange(self.history_samples) - self.history_samples) * 1000.0 / self.sample_rate_hz
        for index, (curve, channel) in enumerate(zip(self.curves, self.channels, strict=True)):
            curve.setData(x_ms, self.buffer[:, channel] * 0.72 + index * 2.0)


class SpectrumView(QWidget):
    def __init__(self, show_enhanced: bool = True) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.plot = pg.PlotWidget()
        _configure_plot(self.plot, "频率 / Hz", "归一化幅度 / dB")
        self.plot.setXRange(0, 4000, padding=0)
        self.plot.setYRange(-80, 3, padding=0)
        self.plot.showGrid(x=True, y=True, alpha=0.16)
        self.raw_curve = self.plot.plot(name="原始 CH1", pen=pg.mkPen("#49BFE3", width=1), antialias=False)
        self.enhanced_curve = None
        if show_enhanced:
            self.enhanced_curve = self.plot.plot(name="WDAS 输出", pen=pg.mkPen("#F0B44D", width=1), antialias=False)
            self.plot.addLegend(offset=(-8, 8), labelTextColor="#DCE7EF")
        layout.addWidget(self.plot)

    def update_frame(self, frame: AcousticFrame) -> None:
        self.raw_curve.setData(frame.frequency_hz, frame.raw_spectrum_db)
        if self.enhanced_curve is not None:
            self.enhanced_curve.setData(frame.frequency_hz, frame.enhanced_spectrum_db)

    def update_raw(self, frequency_hz: np.ndarray, spectrum_db: np.ndarray) -> None:
        self.raw_curve.setData(frequency_hz, spectrum_db)

    def update_enhanced(self, frequency_hz: np.ndarray, spectrum_db: np.ndarray) -> None:
        if self.enhanced_curve is not None:
            self.enhanced_curve.setData(frequency_hz, spectrum_db)


class WaveformComparisonView(QWidget):
    def __init__(self, history_samples: int = 4096) -> None:
        super().__init__()
        self.history_samples = history_samples
        self.sample_rate_hz = 16_000
        self.raw = np.zeros(history_samples, dtype=np.float32)
        self.enhanced = np.zeros(history_samples, dtype=np.float32)
        self._raw_identity = self._enhanced_identity = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.plot = pg.PlotWidget()
        _configure_plot(self.plot, "时间 / ms", "归一化幅度")
        self.plot.setYRange(-1.1, 3.0, padding=0.03)
        self.plot.showGrid(x=True, y=True, alpha=0.13)
        self.raw_curve = self.plot.plot(name="原始 CH1", pen=pg.mkPen("#49BFE3", width=1), antialias=False)
        self.enhanced_curve = self.plot.plot(name="WDAS 输出（+2 偏移）", pen=pg.mkPen("#F0B44D", width=1), antialias=False)
        self.plot.addLegend(offset=(-8, 8), labelTextColor="#DCE7EF")
        layout.addWidget(self.plot)

    def update_frame(self, frame: AcousticFrame) -> None:
        identity = (frame.evidence_source, frame.sample_rate_hz, frame.packet_loss, frame.frame_index)
        self._check_history("raw", identity)
        self._check_history("enhanced", identity)
        size = min(frame.pcm.shape[0], self.history_samples)
        self.raw = np.roll(self.raw, -size)
        self.enhanced = np.roll(self.enhanced, -size)
        self.raw[-size:] = frame.pcm[-size:, 0]
        self.enhanced[-size:] = frame.enhanced[-size:]
        self.sample_rate_hz = frame.sample_rate_hz
        x_ms = (np.arange(self.history_samples) - self.history_samples) * 1000.0 / self.sample_rate_hz
        self.raw_curve.setData(x_ms, self.raw * 0.72)
        self.enhanced_curve.setData(x_ms, self.enhanced * 0.72 + 2.0)

    def _check_history(self, kind, identity):
        previous = getattr(self, "_" + kind + "_identity")
        if previous is not None and (identity[:3] != previous[:3] or identity[3] != previous[3] + 1):
            getattr(self, kind).fill(np.nan)
        setattr(self, "_" + kind + "_identity", identity)

    def update_raw(self, samples: np.ndarray, sample_rate_hz: float, *, frame_index=None, evidence_source="", packet_loss=0) -> None:
        if frame_index is not None:
            self._check_history("raw", (evidence_source, sample_rate_hz, packet_loss, frame_index))
        size = min(samples.size, self.history_samples)
        self.raw = np.roll(self.raw, -size)
        self.raw[-size:] = samples[-size:]
        self.sample_rate_hz = float(sample_rate_hz)
        x_ms = (np.arange(self.history_samples) - self.history_samples) * 1000.0 / self.sample_rate_hz
        self.raw_curve.setData(x_ms, self.raw * 0.72)
        self.enhanced_curve.setData(x_ms, self.enhanced * 0.72 + 2.0)

    def update_enhanced(self, samples: np.ndarray, sample_rate_hz: float, *, frame_index=None, evidence_source="") -> None:
        if frame_index is not None:
            self._check_history("enhanced", (evidence_source, sample_rate_hz, 0, frame_index))
        size = min(samples.size, self.history_samples)
        self.enhanced = np.roll(self.enhanced, -size)
        self.enhanced[-size:] = samples[-size:]
        self.sample_rate_hz = float(sample_rate_hz)
        x_ms = (np.arange(self.history_samples) - self.history_samples) * 1000.0 / self.sample_rate_hz
        self.raw_curve.setData(x_ms, self.raw * 0.72)
        self.enhanced_curve.setData(x_ms, self.enhanced * 0.72 + 2.0)


class SoundFieldView(QWidget):
    def __init__(self) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.plot = pg.PlotWidget()
        _configure_plot(self.plot, "方位角 Azimuth / deg", "俯仰角 Elevation / deg")
        self.plot.setAspectLocked(False)
        self.plot.setXRange(-60, 60, padding=0)
        self.plot.setYRange(-30, 30, padding=0)
        self.plot.showGrid(x=True, y=True, alpha=0.20)
        self.image = pg.ImageItem(axisOrder="row-major")
        color_map = pg.colormap.get("CET-L9")
        self.image.setLookupTable(color_map.getLookupTable(nPts=256))
        self.image.setLevels((-30.0, 0.0))
        self.plot.addItem(self.image)
        self.candidates = pg.ScatterPlotItem(
            size=11,
            symbol="o",
            pen=pg.mkPen("#DCE7EF", width=1.5),
            brush=pg.mkBrush(0, 0, 0, 0),
        )
        self.sources = pg.ScatterPlotItem(
            size=14,
            symbol="x",
            pen=pg.mkPen("#111820", width=3),
            brush=pg.mkBrush(0, 0, 0, 0),
        )
        self.plot.addItem(self.candidates)
        self.plot.addItem(self.sources)
        self.source_labels: list[pg.TextItem] = []
        layout.addWidget(self.plot)

    def update_frame(self, frame: AcousticFrame) -> None:
        self.image.setImage(frame.response_db, autoLevels=False, levels=(-30.0, 0.0))
        self.image.setRect(
            QRectF(
                float(frame.azimuth_axis_deg[0]),
                float(frame.elevation_axis_deg[0]),
                float(frame.azimuth_axis_deg[-1] - frame.azimuth_axis_deg[0]),
                float(frame.elevation_axis_deg[-1] - frame.elevation_axis_deg[0]),
            )
        )
        self.candidates.setData(
            [item.azimuth_deg for item in frame.candidates],
            [item.elevation_deg for item in frame.candidates],
        )
        self.update_sources(frame.sources)

    def clear_evidence(self) -> None:
        self.image.setImage(np.full((61, 121), -30.0, dtype=np.float32), autoLevels=False, levels=(-30.0, 0.0))
        self.image.setRect(QRectF(-60.0, -30.0, 120.0, 60.0))
        self.candidates.setData([], [])
        self.update_sources(())

    def update_sources(self, sources: tuple[SourceEstimate, ...]) -> None:
        self.sources.setData(
            [item.azimuth_deg for item in sources],
            [item.elevation_deg for item in sources],
        )
        for item in self.source_labels:
            self.plot.removeItem(item)
        self.source_labels.clear()
        for source in sources:
            label = pg.TextItem(
                f"{source.source_id}  {source.azimuth_deg:+.1f}°, {source.elevation_deg:+.1f}°",
                color="#F7FBFF",
                fill=pg.mkBrush(14, 28, 41, 205),
                border=pg.mkPen("#7EDAE7"),
                anchor=(0, 1),
            )
            label.setPos(source.azimuth_deg + 1.5, source.elevation_deg - 1.2)
            self.plot.addItem(label)
            self.source_labels.append(label)


class SourceTable(QTableWidget):
    def __init__(self) -> None:
        super().__init__(0, 5)
        self.setHorizontalHeaderLabels(("ID", "方位角", "俯仰角", "证据", "状态"))
        self.verticalHeader().setVisible(False)
        self.setAlternatingRowColors(True)
        self.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.horizontalHeader().setStretchLastSection(True)
        self.setMaximumHeight(154)

    def update_frame(self, frame: AcousticFrame) -> None:
        self.update_sources(frame.sources)

    def update_sources(self, sources: tuple[SourceEstimate, ...]) -> None:
        self.setRowCount(len(sources))
        for row, source in enumerate(sources):
            values = (
                source.source_id,
                f"{source.azimuth_deg:+.1f}°",
                f"{source.elevation_deg:+.1f}°",
                source.evidence_text or f"{source.evidence_db:.1f} dB",
                source.state,
            )
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                if column in {1, 2, 3}:
                    item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                self.setItem(row, column, item)
        self.resizeColumnsToContents()


class HistorySparkline(QWidget):
    def __init__(self, maximum_points: int = 160) -> None:
        super().__init__()
        self.azimuth = deque(maxlen=maximum_points)
        self.elevation = deque(maxlen=maximum_points)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.plot = pg.PlotWidget()
        _configure_plot(self.plot, "帧历史", "角度 / deg")
        self.plot.setYRange(-60, 60, padding=0)
        self.plot.showGrid(x=True, y=True, alpha=0.13)
        self.az_curve = self.plot.plot(name="Az", pen=pg.mkPen("#38C6E5", width=1.5))
        self.el_curve = self.plot.plot(name="El", pen=pg.mkPen("#F0B44D", width=1.5))
        self.plot.addLegend(offset=(-6, 6), labelTextColor="#DCE7EF")
        layout.addWidget(self.plot)

    def update_frame(self, frame: AcousticFrame) -> None:
        self.update_sources(frame.sources)

    def update_sources(self, sources: tuple[SourceEstimate, ...]) -> None:
        if sources:
            self.azimuth.append(sources[0].azimuth_deg)
            self.elevation.append(sources[0].elevation_deg)
        else:
            self.azimuth.append(np.nan)
            self.elevation.append(np.nan)
        self.az_curve.setData(np.asarray(self.azimuth, dtype=float))
        self.el_curve.setData(np.asarray(self.elevation, dtype=float))


def _configure_plot(plot: pg.PlotWidget, bottom_label: str, left_label: str) -> None:
    plot.setBackground(COLORS["chart"])
    plot.setLabel("bottom", bottom_label, color="#BACBD8")
    plot.setLabel("left", left_label, color="#BACBD8")
    plot.getPlotItem().getAxis("bottom").setPen(pg.mkPen("#8396A6"))
    plot.getPlotItem().getAxis("left").setPen(pg.mkPen("#8396A6"))
    plot.getPlotItem().getAxis("bottom").setTextPen(pg.mkPen("#C9D5DF"))
    plot.getPlotItem().getAxis("left").setTextPen(pg.mkPen("#C9D5DF"))
    plot.setMouseEnabled(x=True, y=True)
    plot.setClipToView(True)
    # 只减少绘图用数据，getData()、原始缓冲和导出仍保留
    # 原始样本；峰值模式保留极值，不做平均平滑。
    plot.setDownsampling(auto=True, mode="peak")
    plot.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
