"""显式离线界面验证：已有仿真热图、静态图像和可调整的TEST方向；不连接板卡。"""
from pathlib import Path
from dataclasses import replace
import argparse,time
from PySide6.QtCore import QTimer,Qt
from PySide6.QtGui import QImage,QPainter,QColor,QFont
from PySide6.QtWidgets import QApplication,QHBoxLayout,QLabel,QSpinBox,QCheckBox,QComboBox,QToolBar
from mic16_qt.app import _install_cjk_font
from mic16_qt.presentation.compact_theme import compact_stylesheet
from mic16_qt.presentation.compact_window import CompactWindow
from mic16_qt.core.models import SourceEstimate
from mic16_av_fusion.video_wire import VideoMeta,VideoFrame

ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--screenshot',type=Path);parser.add_argument('--image',type=Path)
    args=parser.parse_args();app=QApplication([]);family=_install_cjk_font();app.setFont(QFont(family,10));app.setStyleSheet(compact_stylesheet(family))
    window=CompactWindow();window.setWindowTitle('MIC16 离线功能预览（不连接板卡）')
    window.load_heatmap_replay(ROOT/'host/demo_assets/meeting_room_1.m16h')
    panel=window.video_panel
    path=args.image or ROOT/'build/board/video_pixel_order_20261021/real_video_corrected/camera_frame_001.png'
    image=QImage(str(path)) if path.is_file() else QImage()
    background='静态历史相机帧'
    if image.isNull():
        background='测试网格图';image=QImage(800,480,QImage.Format.Format_RGB888);image.fill(QColor('#33434f'))
        painter=QPainter(image);painter.setPen(QColor('#70828f'))
        for x in range(0,800,40):painter.drawLine(x,0,x,480)
        for y in range(0,480,40):painter.drawLine(0,y,800,y)
        painter.setPen(QColor('#ffffff'));painter.drawText(25,80,'离线投影测试网格 · 不是摄像头画面');painter.end()
    panel.canvas.image=image.copy();meta=VideoMeta(20261021,1,0,image.width(),image.height(),2,0,1,1,0,image.width()*image.height()*3,0,1)
    panel.last_frame=VideoFrame(meta,b'',time.monotonic());panel.overlay_enabled.setChecked(True)
    panel.listen_button.setEnabled(False);panel.settings_button.setEnabled(False)
    panel.state.setText('离线预览 · 不监听网络')
    row=QHBoxLayout();row.addWidget(QLabel('TEST1 方位 / °'));az=QSpinBox();az.setRange(-60,60);az.setValue(-12);row.addWidget(az)
    row.addWidget(QLabel('俯仰 / °'));el=QSpinBox();el.setRange(-30,30);el.setValue(0);row.addWidget(el)
    second=QCheckBox('第二个测试点');second.setChecked(True);row.addWidget(second);row.addStretch(1)
    panel.layout().insertLayout(1,row)
    toolbar=QToolBar('已有空间响应回放');window.addToolBar(toolbar);toolbar.addWidget(QLabel('独立仿真窗（非连续轨迹）：'))
    rooms=QComboBox()
    for i,count in enumerate([0,1,3,4,0,2,3,4]):rooms.addItem(f'窗{i} · 标签{count}个点源',i)
    rooms.setCurrentIndex(1);toolbar.addWidget(rooms)
    def select_room():
        window.load_heatmap_replay(ROOT/f'host/demo_assets/meeting_room_{rooms.currentData()}.m16h')
        notice()
    rooms.currentIndexChanged.connect(select_room)
    def notice():
        window.notice.setText(f'离线功能预览：热图＝已有仿真C空间响应；视频＝{background}；TEST点＝指定方向，非实测定位。')
        window._badge('离线功能预览 · 非实测')
    def refresh():
        now=time.monotonic();panel.last_frame=replace(panel.last_frame,received_monotonic_s=now)
        points=[SourceEstimate('TEST1',float(az.value()),float(el.value()),0.,'CURRENT','指定测试方向')]
        if second.isChecked():points.append(SourceEstimate('TEST2',15.,8.,0.,'CURRENT','指定测试方向'))
        panel.set_acoustic_sources(points,window='TEST',origin='指定方向测试，非实测定位')
        panel.canvas.message=f'离线叠加预览：{background}＋指定TEST方向，非实测定位'
        panel.canvas.update()
    timer=QTimer(window);timer.timeout.connect(refresh);timer.start(100)
    notice();refresh();window.primary_tabs.setCurrentWidget(panel);window.show()
    if args.screenshot:
        def capture():
            args.screenshot.parent.mkdir(parents=True,exist_ok=True)
            if not window.save_screenshot(args.screenshot):raise RuntimeError('截图失败')
            timer.stop();window.close()
        QTimer.singleShot(600,capture)
    return app.exec()
if __name__=='__main__':raise SystemExit(main())
