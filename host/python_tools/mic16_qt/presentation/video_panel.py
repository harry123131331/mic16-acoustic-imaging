"""独立视频与可选粗对齐方向标记；精确标定和物理同步不由预览替代。"""
from __future__ import annotations

import time
import json
from collections import deque
from pathlib import Path
from PySide6.QtCore import Qt, QTimer, QRectF, QPointF, Signal
from PySide6.QtGui import QImage, QPainter, QColor, QPen, QRadialGradient
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QDialog,
    QFormLayout, QLineEdit, QSpinBox, QDoubleSpinBox, QCheckBox, QFileDialog, QDialogButtonBox, QComboBox,
)
from mic16_av_fusion.video_receiver import VideoReceiver
from mic16_qt.presentation.usb_camera_preview import (
    UsbCameraPreview, camera_devices, device_key, format_key, format_label, preferred_format,
)
from mic16_av_fusion.video_wire import to_rgb888
from mic16_av_fusion.preview_projection import PreviewAlignment, project_direction, profile_dict, load_profile, align_direction_to_point
from mic16_av_fusion.preview_alignment_store import video_binding,save_bound_alignment,load_bound_alignment


class VideoCanvas(QWidget):
    point_selected=Signal(float,float)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.image = QImage()
        self.message = '等待视频数据'
        self.markers = ()
        self.spatial_overlay=QImage()
        self.point_selection=False
        self.display_mirror_x=False
        self.setMinimumSize(320, 180)

    def image_rect(self):
        if self.image.isNull():return QRectF()
        size=self.image.size().scaled(self.size(),Qt.AspectRatioMode.KeepAspectRatio)
        return QRectF((self.width()-size.width())/2,(self.height()-size.height())/2,size.width(),size.height())

    def select_point(self,enabled):
        self.point_selection=bool(enabled)
        self.setCursor(Qt.CursorShape.CrossCursor if enabled else Qt.CursorShape.ArrowCursor)

    def mousePressEvent(self,event):
        if self.point_selection and event.button()==Qt.MouseButton.LeftButton:
            rect=self.image_rect();point=event.position()
            if not rect.isEmpty() and rect.contains(point):
                u=(point.x()-rect.left())/rect.width();v=(point.y()-rect.top())/rect.height()
                # 点选反变换回原视频坐标；显示翻转不改变已保存的声学投影参数。
                if self.display_mirror_x:u=min(1.-1e-12,1.-u)
                if 0<=u<1 and 0<=v<1:self.point_selected.emit(u,v)
            event.accept();return
        super().mousePressEvent(event)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor('#00032e'))
        if not self.image.isNull():
            rect=self.image_rect()
            painter.save()
            if self.display_mirror_x:
                # 图像与真实响应使用同一个显示变换，禁止只翻视频或只翻热图。
                painter.translate(rect.left()+rect.right(),0);painter.scale(-1,1)
            painter.drawImage(rect, self.image)
            if not self.spatial_overlay.isNull():
                painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform,True)
                painter.drawImage(rect,self.spatial_overlay)
            painter.restore()
            # 所有坐标在实际画面矩形内缩放，保留黑边，不按整个控件拉伸。
            for marker in self.markers:
                u,v,ru,rv,identity=marker
                if self.display_mirror_x:u=1-u
                center=QPointF(rect.left()+u*rect.width(),rect.top()+v*rect.height())
                painter.save();painter.setClipRect(rect)
                # 红黄热斑是已检出方向的显示范围，不是从方向点伪造的声压/空间响应。
                rx,ry=max(12.,ru*rect.width()),max(12.,rv*rect.height())
                painter.translate(center);painter.scale(rx,ry)
                glow=QRadialGradient(QPointF(0,0),1.)
                for at,color in [(0.,QColor(245,28,20,225)),(.30,QColor(255,65,0,210)),
                                 (.52,QColor(255,235,0,180)),(.72,QColor(0,225,220,95)),
                                 (1.,QColor(0,80,255,0))]:glow.setColorAt(at,color)
                painter.setRenderHint(QPainter.RenderHint.Antialiasing,True)
                painter.setPen(Qt.PenStyle.NoPen);painter.setBrush(glow)
                painter.drawEllipse(QPointF(0,0),1.,1.)
                painter.restore();painter.save();painter.setClipRect(rect)
                painter.setPen(QColor('#ffffff'));painter.drawText(center+QPointF(rx+5,-ry-2),str(identity))
                painter.restore()
        if self.message:
            painter.fillRect(0, 0, self.width(), 36, QColor(18, 25, 30, 220))
            painter.setPen(QColor('#f0f4f6'))
            painter.drawText(self.rect().adjusted(12, 8, -8, -8),
                             Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop, self.message)


class VideoPanel(QWidget):
    def __init__(self, parent=None, *, default_addresses=None, settings_path=None):
        super().__init__(parent)
        self._default_addresses = default_addresses
        self.listen_ip, self.board_ip, self.port = '0.0.0.0', '192.168.1.10', 5002
        self._configured = False
        self.receiver = None
        self.usb_receiver = None
        self.source_kind = 'usb'
        self.overlay_extent_percent=50
        self.usb_device_id = ''
        self.usb_format_key = None
        self.display_mirror_x=False
        self._settings_path = (Path(settings_path) if settings_path is not None else
                               Path(__file__).resolve().parents[4] / 'build/config/usb_camera_preview.json')
        self._read_usb_selection()
        self._usb_factory = UsbCameraPreview
        self._stopping = False
        self.last_frame = None
        self.alignment=PreviewAlignment()
        self._alignment_path=self._settings_path.with_name('preview_alignment.json')
        self._bearing_path=self._settings_path.with_name('bearing_image_alignment.json')
        self._bearing_map=None;self._bearing_roll=None;self._bearing_validation=None
        self._multi_points=None;self._multi_candidate=None;self._multi_error=None
        self._recent_directions=deque(maxlen=8)
        self._pending_direction=None;self._pending_since=None
        self._alignment_binding=None;self._alignment_reference=None
        self._alignment_note='尚未保存当前相机的对齐参数'
        self._acoustic_sources=();self._acoustic_received=None;self._acoustic_window=None
        self._acoustic_origin='';self._overlay_binding=None
        self._physical_response=None;self._physical_mode=False;self._response_levels=(.1,.35)
        self._physical_overlay_key=None
        self._physical_overlay_has_signal=False
        layout = QVBoxLayout(self)
        row = QHBoxLayout()
        self.listen_button = QPushButton('打开摄像头')
        self.listen_button.clicked.connect(self._toggle)
        row.addWidget(self.listen_button)
        self.settings_button = QPushButton('视频设置')
        self.settings_button.clicked.connect(self._settings)
        row.addWidget(self.settings_button)
        self.overlay_enabled=QCheckBox('叠加声源热斑')
        self.overlay_enabled.setToolTip('方向标记从红向蓝渐变，边缘透明；渐变用于显示，完整真实空间响应在定位页。默认参数未精确标定。')
        self.overlay_enabled.toggled.connect(self._overlay_toggled)
        row.addWidget(self.overlay_enabled)
        row.addWidget(QLabel('热斑大小'))
        self.overlay_extent=QComboBox()
        self.overlay_extent.setObjectName('OverlayExtent')
        for label,extent in [('小',50),('中',75),('大',100)]:self.overlay_extent.addItem(label,extent)
        self.overlay_extent.setCurrentIndex(self.overlay_extent.findData(self.overlay_extent_percent))
        self.overlay_extent.setToolTip('按小／中／大限制有效方向附近的真实响应显示区域，减少遮挡；不是实际声源尺寸或阵列分辨率')
        self.overlay_extent.currentIndexChanged.connect(self._overlay_extent_changed)
        row.addWidget(self.overlay_extent)
        self.alignment_button=QPushButton('对齐设置')
        self.alignment_button.clicked.connect(self._alignment_settings)
        row.addWidget(self.alignment_button)
        self.point_alignment_button=QPushButton('点选对齐')
        self.point_alignment_button.setToolTip('先保持声源静止，再点击画面中实际声源的位置；只调整显示偏移，不改变定位结果')
        self.point_alignment_button.clicked.connect(self._begin_point_alignment)
        row.addWidget(self.point_alignment_button)
        self.display_flip=QCheckBox('左右翻转')
        self.display_flip.setToolTip('同时翻转视频和热图，修正画面左右；不修改阵列方向角或定位结果')
        self.display_flip.setChecked(self.display_mirror_x)
        self.display_flip.toggled.connect(self._display_mirror_toggled)
        row.addWidget(self.display_flip)
        self.state = QLabel('USB相机未打开 · 点击打开' if self.usb_device_id else 'USB相机未打开 · 请先选择设备')
        row.addWidget(self.state, 1)
        layout.addLayout(row)
        self.canvas = VideoCanvas()
        self.canvas.display_mirror_x=self.display_mirror_x
        self.canvas.point_selected.connect(self._align_to_video_point)
        layout.addWidget(self.canvas, 1)
        self.detail = QLabel('USB视频直接由电脑采集；定位由板卡发送，两路时间与坐标仍需核验。')
        self.detail.setObjectName('MutedLabel')
        self.detail.setWordWrap(True)
        layout.addWidget(self.detail)
        self._timer = QTimer(self)
        self._timer.setInterval(33)
        self._timer.timeout.connect(self._poll)
        self._timer.start()

    def _read_usb_selection(self):
        """只恢复设备选择，不枚举相机、不自动采集；设备消失时禁止回退内置相机。"""
        try:
            data = json.loads(self._settings_path.read_text(encoding='utf-8'))
            identity, fmt = data['device_id'], data['format']
            if data.get('version') != 1 or not isinstance(identity, str) or not bytes.fromhex(identity):
                return
            if fmt is not None and (not isinstance(fmt, list) or len(fmt) != 5):
                return
            self.usb_device_id = identity
            self.usb_format_key = None if fmt is None else tuple(fmt)
            mirrored=data.get('display_mirror_x',False)
            if type(mirrored) is bool:self.display_mirror_x=mirrored
            extent=data.get('overlay_extent_percent',50)
            if type(extent) is int and extent in (50,75,100):self.overlay_extent_percent=extent
        except (OSError, ValueError, KeyError, TypeError):
            pass

    def _save_usb_selection(self):
        try:
            self._settings_path.parent.mkdir(parents=True, exist_ok=True)
            self._settings_path.write_text(json.dumps(dict(version=1, device_id=self.usb_device_id,
                format=self.usb_format_key,display_mirror_x=self.display_mirror_x,
                overlay_extent_percent=self.overlay_extent_percent), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        except OSError:
            # 设备仍可预览；保存失败不使相机与板端采集停止。
            self.detail.setText('相机选择未能保存；下次打开软件时请重新选择设备。')

    def _display_mirror_toggled(self):
        self.display_mirror_x=self.display_flip.isChecked()
        self.canvas.display_mirror_x=self.display_mirror_x
        self._save_usb_selection();self.canvas.update()

    def _overlay_extent_changed(self):
        self.overlay_extent_percent=self.overlay_extent.currentData()
        self._physical_overlay_key=None
        self._save_usb_selection();self._update_overlay()

    def _restore_alignment_for_frame(self,frame):
        binding=video_binding(frame.meta)
        if binding==self._alignment_binding:return
        # 不同相机、模式或画幅不能复用刚才的显示偏移。
        if self._alignment_binding is not None:self.alignment=PreviewAlignment()
        self._alignment_binding=binding;self._alignment_reference=None
        self._physical_overlay_key=None
        self._bearing_map=None;self._bearing_roll=None;self._bearing_validation=None
        self._multi_points=None;self._multi_candidate=None;self._multi_error=None;self._recent_directions.clear()
        self.canvas.select_point(False);self.point_alignment_button.setText('点选对齐')
        try:
            self.alignment,self._alignment_reference=load_bound_alignment(self._alignment_path,binding)
            self._alignment_note='已恢复当前相机的人工对齐参数'
        except FileNotFoundError:
            self._alignment_note='当前相机尚未保存对齐参数'
        except (OSError,ValueError,TypeError,KeyError) as error:
            self._alignment_note='未沿用已保存参数：'+str(error)
        self._bearing_path=self._alignment_path.with_name('bearing_image_alignment.json')
        try:
            from mic16_av_fusion.bearing_image_alignment import load_bearing_alignment
            self._bearing_map,self._bearing_validation=load_bearing_alignment(self._bearing_path,binding,self.alignment.roll_deg)
            self._bearing_roll=self.alignment.roll_deg
            self._alignment_note=f'已恢复人工多点对齐；独立核验{self._bearing_validation["error_pixels"]:.1f}像素'
        except FileNotFoundError:pass
        except (OSError,ValueError,TypeError,KeyError) as error:
            self._alignment_note+='；未沿用多点参数：'+str(error)

    def _persist_alignment(self):
        if self._alignment_binding is None:
            self._alignment_note='参数仅本次预览使用；打开相机后再应用以绑定画幅'
            return False
        try:
            save_bound_alignment(self._alignment_path,self.alignment,self._alignment_binding,self._alignment_reference)
            self._alignment_note='已保存当前相机的人工对齐参数'
            return True
        except (OSError,ValueError) as error:
            self._alignment_note='对齐参数本次生效，但未保存：'+str(error)
            return False

    def _alignment_input(self):
        now=time.monotonic();pair=self._physical_response;frame=self.last_frame
        if frame is None or self.canvas.image.isNull() or not 0<=now-frame.received_monotonic_s<1.:
            raise ValueError('请先打开实时摄像头')
        if (not self._physical_mode or pair is None or not pair[0].valid or not pair[0].map_valid or
                not 0<=now-pair[0].received_monotonic_s<1. or not 0<=now-pair[1].received_monotonic_s<1.):
            raise ValueError('请等待新的有效方向和对应响应，声源保持静止')
        if self._pending_direction is not None:raise ValueError('正在等待当前方向的完整响应，请稍候再点选')
        return pair[0],frame

    def _begin_point_alignment(self):
        self._multi_points=None
        if self.canvas.point_selection:
            self.canvas.select_point(False);self.point_alignment_button.setText('点选对齐');self._update_overlay();return
        if self._active_bearing_map() is not None:
            self.detail.setText('已有多点对齐；如需单点偏移，请先在对齐设置中清除多点对齐。');return
        try:self._alignment_input()
        except ValueError as error:
            self.detail.setText(str(error));return
        self.overlay_enabled.setChecked(True)
        self.canvas.select_point(True);self.point_alignment_button.setText('取消点选')
        self.canvas.message='保持声源静止，点击画面中实际声源的位置'
        self.canvas.update()

    def _align_to_video_point(self,u,v):
        if self._multi_points is not None:
            self._collect_multi_point(u,v);return
        try:
            direction,frame=self._alignment_input()
            binding=video_binding(frame.meta)
            if binding!=self._alignment_binding:raise ValueError('相机配置已改变，请重新选择对齐')
            alignment=align_direction_to_point(direction.azimuth_deg,direction.elevation_deg,u,v,self.alignment)
            self.alignment=alignment
            self._alignment_reference=dict(method='MANUAL_STATIONARY_SOURCE_POINT',
                acoustic_key=list(direction.key),azimuth_deg=direction.azimuth_deg,elevation_deg=direction.elevation_deg,
                video_frame=frame.meta.frame_id,video_epoch=frame.meta.epoch,u=u,v=v,
                acoustic_receipt_age_s=time.monotonic()-direction.received_monotonic_s,
                physical_sync_verified=False)
            self._persist_alignment();self._overlay_binding=None;self._physical_overlay_key=None
        except (ValueError,OSError) as error:
            self._alignment_note='本次点选未应用：'+str(error)
        finally:
            self.canvas.select_point(False);self.point_alignment_button.setText('点选对齐')
            self._update_overlay()
            self.detail.setText(self._alignment_note+'；单点对齐不校验视场、距离、畸变或声音到画面的延迟。')

    def _active_bearing_map(self):
        return self._bearing_map if self._bearing_roll==self.alignment.roll_deg else None

    def _begin_multi_alignment(self):
        try:self._alignment_input()
        except ValueError as error:self.detail.setText(str(error));return
        self._multi_points=[];self._multi_candidate=None;self._multi_error=None
        self.overlay_enabled.setChecked(True);self.canvas.select_point(True)
        self.point_alignment_button.setText('取消多点')
        self._multi_instruction()

    def _multi_instruction(self):
        if self._multi_points is None:return
        count=len(self._multi_points)
        targets=['左上','右上','左下','右下','中间（独立核验）']
        self.detail.setText(f'多点对齐 {count+1}/5：把发声设备移到画面{targets[count]}附近，保持约1～2米和相近距离；'
                            '停稳至少2秒，再点实际扬声器出声位置。固定相机和阵列，四角不必贴到边缘。'
                            +(' '+self._multi_error if self._multi_error else ''))
        self.canvas.message=f'多点对齐 {count+1}/5 · {targets[count]}：停稳后点击出声位置';self.canvas.update()

    def _collect_multi_point(self,u,v):
        try:
            direction,frame=self._alignment_input()
            if not all(isinstance(x,(int,float)) and not isinstance(x,bool) and 0<=x<1 for x in (u,v)):
                raise ValueError('请点击实际视频画面内的位置')
            import numpy as np
            from mic16_av_fusion.bearing_image_alignment import bearing,fit_bearing_image_map,check_holdout,save_bearing_alignment
            recent=list(self._recent_directions)[-3:]
            if len(recent)!=3 or any(not d.valid or d.epoch_key!=direction.epoch_key for d in recent) or any(b.sequence!=a.sequence+1 for a,b in zip(recent,recent[1:])):
                raise ValueError('还没有连续三个有效方向，请保持声源静止至少2秒')
            if recent[-1].key!=direction.key or time.monotonic()-recent[0].received_monotonic_s>2.:
                raise ValueError('静态参照已过期，请等待新的连续有效结果')
            vectors=np.array([bearing(d.azimuth_deg,d.elevation_deg) for d in recent])
            center=np.median(vectors,axis=0);center/=np.linalg.norm(center)
            spread=np.rad2deg(np.arccos(np.clip(vectors@center,-1.,1.))).max()
            if spread>7.5:raise ValueError('方向仍在跳动，本次点不采纳；请停稳后再点')
            angles=[float(np.rad2deg(np.arctan2(center[0],center[2]))),float(np.rad2deg(np.arctan2(center[1],np.hypot(center[0],center[2]))))]
            for old in self._multi_points:
                separation=float(np.rad2deg(np.arccos(np.clip(bearing(*old['angles'])@center,-1.,1.))))
                if separation<(4. if len(self._multi_points)==4 else 7.5):raise ValueError('与已有方向太接近，请把发声设备移到另一个位置')
            record=dict(angles=angles,uv=[float(u),float(v)],sample_keys=[list(d.key) for d in recent],
                video_frame=frame.meta.frame_id,static_spread_deg=float(spread),precise_time_sync_verified=False)
            points=self._multi_points+[record]
            if len(points)==4:
                self._multi_candidate=fit_bearing_image_map(points)
            if len(points)<5:
                self._multi_points=points;self._multi_error=None;self._multi_instruction();return
            validation=check_holdout(self._multi_candidate,record,frame.meta.width,frame.meta.height)
            if not validation['passed']:
                raise ValueError(f'独立核验偏差{validation["error_pixels"]:.1f}像素，超过{validation["limit_pixels"]:.1f}像素；新参数未采用，请重新四点对齐')
            self._bearing_path=self._alignment_path.with_name('bearing_image_alignment.json')
            save_bearing_alignment(self._bearing_path,self._multi_candidate,video_binding(frame.meta),self.alignment.roll_deg,points,validation)
            self._bearing_map=self._multi_candidate;self._bearing_roll=self.alignment.roll_deg;self._bearing_validation=validation
            self._multi_points=None;self._multi_error=None;self.canvas.select_point(False);self.point_alignment_button.setText('点选对齐')
            self._physical_overlay_key=None;self._alignment_note=f'人工多点对齐已保存；独立核验{validation["error_pixels"]:.1f}像素'
            self._update_overlay();self.detail.setText(self._alignment_note+'；仅适用于固定安装、相近工作平面，未核验时间同步。')
        except (ValueError,OSError,TypeError) as error:
            self._multi_error='此点未采纳：'+str(error);self._multi_instruction()

    def _clear_multi_alignment(self):
        # 仅由用户显式点击清除本应用的多点配置，原人工偏角文件仍保留。
        try:self._bearing_path.unlink(missing_ok=True)
        except OSError as error:self.detail.setText('多点配置未能清除：'+str(error));return
        self._bearing_map=None;self._bearing_roll=None;self._bearing_validation=None;self._multi_points=None
        self.canvas.select_point(False);self.point_alignment_button.setText('点选对齐')
        self._physical_overlay_key=None;self._update_overlay()
        self.detail.setText('已清除多点对齐，当前使用原人工偏角/视场参数。')

    def expect_spatial_response(self,direction):
        old=self._physical_response
        if old is not None and old[0].key==direction.key:return
        if old is not None and old[0].epoch_key!=direction.epoch_key:self.clear_spatial_response()
        self._pending_direction=direction;self._pending_since=time.monotonic()

    def overlay_snapshot(self):
        """只报告当前真正可绘制的响应与视频身份，不把接收时间当物理同步。"""
        try:direction,frame=self._alignment_input()
        except ValueError:return None
        if (not self.overlay_enabled.isChecked() or self.canvas.spatial_overlay.isNull() or
                not self._physical_overlay_has_signal or
                self._physical_overlay_key is None or self._physical_overlay_key[0]!=direction.key):return None
        return dict(key=list(direction.key),video_frame=frame.meta.frame_id,video_epoch=frame.meta.epoch,
                    video_width=frame.meta.width,video_height=frame.meta.height,
                    display_mirror_x=self.display_mirror_x,
                    overlay_extent_percent=self.overlay_extent_percent,
                    overlay_render_mode='REAL_RESPONSE_GATED_DIRECTION_MARKER_COLOR_FALLOFF',
                    color_falloff_display_only=True,raw_response_modified=False,
                    overlay_radius_deg=max(.1,self.alignment.radius_deg*self.overlay_extent_percent/100.),
                    multipoint_alignment=self._active_bearing_map() is not None,
                    alignment_settings=profile_dict(self.alignment)['settings'],
                    acoustic_received_ns=round(direction.received_monotonic_s*1e9),
                    response_received_ns=round(self._physical_response[1].received_monotonic_s*1e9),
                    video_received_ns=round(frame.received_monotonic_s*1e9),
                    physical_sync_verified=False)

    def clear_spatial_response(self):
        self._physical_mode=True;self._physical_response=None;self._acoustic_sources=()
        self._physical_overlay_key=None
        self._pending_direction=None;self._pending_since=None;self._recent_directions.clear()
        self.canvas.markers=();self.canvas.spatial_overlay=QImage();self.canvas.update()

    def set_spatial_response(self,direction,response,levels=(.1,.35)):
        from mic16_av_fusion.fast_display_wire import same_observation
        if not same_observation(direction,response):raise ValueError('声像响应与方向采样身份不一致')
        if self._pending_direction is not None and self._pending_direction.key!=direction.key:return
        self._pending_direction=None;self._pending_since=None
        if not self._recent_directions or self._recent_directions[-1].key!=direction.key:self._recent_directions.append(direction)
        self._physical_mode=True;self._physical_response=(direction,response);self._response_levels=tuple(levels)
        self._acoustic_sources=();self._update_overlay()

    def _update_physical_overlay(self):
        from mic16_av_fusion.spatial_response_overlay import response_rgba
        from mic16_av_fusion.hemisphere_grid import grid_geometry
        self.canvas.markers=()
        def clear(message=None):
            self._physical_overlay_key=None;self._physical_overlay_has_signal=False;self.canvas.spatial_overlay=QImage()
            if message is not None:self.canvas.message=message
            self.canvas.update()
        if not self.overlay_enabled.isChecked():clear();return
        now=time.monotonic();pair=self._physical_response;frame=self.last_frame
        if self._pending_direction is not None and now-self._pending_since>=.08:
            clear('等待当前方向的完整热图');return
        if frame is None or self.canvas.image.isNull():clear('等待摄像头画面');return
        if not 0<=now-frame.received_monotonic_s<1.:clear('视频已中断');return
        if (pair is None or not 0<=now-pair[0].received_monotonic_s<1. or
                not 0<=now-pair[1].received_monotonic_s<1. or not pair[0].valid):
            clear('等待有效定位');return
        meta=frame.meta;binding=(meta.width,meta.height,meta.mount_id,meta.mode_id,meta.transform_id)
        if self._overlay_binding is None:self._overlay_binding=binding
        if binding!=self._overlay_binding:
            clear('图像配置已改变，请重新启用叠加');return
        mapping=self._active_bearing_map()
        key=(pair[0].key,id(pair[1]),self.alignment,self._response_levels,binding,self.overlay_extent_percent,mapping)
        if key != self._physical_overlay_key:
            # 声学响应约每0.47秒更新一次；同一热图复用，不跟随每个视频帧重复投影。
            rgba=response_rgba(pair[1].values,self.alignment,640,360,self._response_levels,
                               extent_percent=self.overlay_extent_percent,
                               focus_direction=(pair[0].azimuth_deg,pair[0].elevation_deg),bearing_map=mapping,
                               scan_geometry=grid_geometry(pair[0]))
            self._physical_overlay_has_signal=bool(rgba[:,:,3].any())
            self.canvas.spatial_overlay=QImage(rgba.data,rgba.shape[1],rgba.shape[0],rgba.strides[0],QImage.Format.Format_RGBA8888).copy()
            self._physical_overlay_key=key
        self.canvas.message=(f'多点对齐 {len(self._multi_points)+1}/5' if self._multi_points is not None else
            '保持声源静止，点击画面中实际声源的位置' if self.canvas.point_selection else
            '人工多点对齐 · 未核验时间同步' if mapping is not None and self._physical_overlay_has_signal else
            '声像坐标未标定' if self._physical_overlay_has_signal else '方向有效，当前显示范围内无可见热图区')
        if self._multi_points is not None:self._multi_instruction()
        else:self.detail.setText(self._alignment_note+'；真实响应按人工参数投影，未核验任意距离、畸变或物理同步。')
        self.canvas.update()

    def set_acoustic_sources(self,sources,*,received_monotonic_s=None,window=None,origin='UDP_RECEIVED_UNVERIFIED'):
        self._physical_mode=False;self._physical_response=None;self.canvas.spatial_overlay=QImage()
        values=tuple(s for s in sources if s.state not in ('HELD','RETIRED','LOST'))
        if len(values)>8:raise ValueError('方向预览最多8个声源')
        # 接收前校验全部方向，不给非法输入画点。
        for source in values:project_direction(source.azimuth_deg,source.elevation_deg,self.alignment)
        self._acoustic_sources=values
        self._acoustic_received=time.monotonic() if received_monotonic_s is None else float(received_monotonic_s)
        self._acoustic_window=window;self._acoustic_origin=str(origin)
        self._update_overlay()

    def _overlay_toggled(self):
        self._overlay_binding=None
        self._update_overlay()

    def _update_overlay(self):
        if self._physical_mode:return self._update_physical_overlay()
        self.canvas.markers=()
        if not self.overlay_enabled.isChecked():self.canvas.update();return
        now=time.monotonic();frame=self.last_frame
        if frame is None or self.canvas.image.isNull():
            self.canvas.message='等待视频 · 未绘制声源标记';self.canvas.update();return
        if not 0<=now-frame.received_monotonic_s<1.:
            self.canvas.message='视频已过期 · 声源标记隐藏';self.canvas.update();return
        meta=frame.meta;binding=(meta.width,meta.height,meta.mount_id,meta.mode_id,meta.transform_id)
        if self._overlay_binding is None:self._overlay_binding=binding
        if binding!=self._overlay_binding:
            self.canvas.message='图像几何配置改变 · 请重新启用粗对齐';self.canvas.update();return
        if self._acoustic_received is None or not 0<=now-self._acoustic_received<5.:
            self.canvas.message='等待有效声学方向 · 旧点已隐藏';self.canvas.update();return
        markers=[]
        for source in self._acoustic_sources:
            point=project_direction(source.azimuth_deg,source.elevation_deg,self.alignment)
            if point is not None:markers.append((*point,source.source_id))
        self.canvas.markers=tuple(markers)
        age=now-self._acoustic_received
        if self._acoustic_sources and not markers:
            self.canvas.message=f'检出{len(self._acoustic_sources)}源，均在当前估计视场外 · 请检查对齐设置'
        elif not self._acoustic_sources:
            self.canvas.message=f'本窗未检出声源 · 窗{self._acoustic_window} · 接收后{age:.1f}s'
        else:
            self.canvas.message=f'粗对齐方向预览 · 窗{self._acoustic_window} · 接收后{age:.1f}s · 未核验同步'
        self.detail.setText('初始视场角为估计值；可在“对齐设置”调整。相机/阵列平移与畸变未补偿；标记不判断哪个物体发声。'
            +f' 当前{len(markers)}个视野内标记，{len(self._acoustic_sources)-len(markers)}个在视场外；热斑只表示方向区域，不是声压热图。来源：{self._acoustic_origin}')
        self.canvas.update()

    def _alignment_settings(self):
        dialog=QDialog(self);dialog.setWindowTitle('声像对齐设置');form=QFormLayout(dialog)
        dialog.setMinimumWidth(560)
        original_alignment=self.alignment
        fields={}
        for key,label,lo,hi in [('yaw_deg','画面中心方位 / °',-180,180),('pitch_deg','画面中心俯仰 / °',-89,89),
            ('roll_deg','阵列安装旋转 / °',-180,180),
            ('horizontal_fov_deg','水平视场角估计 / °',5,170),('vertical_fov_deg','垂直视场角估计 / °',5,170),
            ('offset_u','水平偏移 / 画面宽度',-.5,.5),('offset_v','垂直偏移 / 画面高度',-.5,.5),('radius_deg','热斑显示半径 / °',.1,15)]:
            control=QDoubleSpinBox();control.setDecimals(3);control.setRange(lo,hi);control.setValue(getattr(self.alignment,key))
            form.addRow(label,control);fields[key]=control
        for key,label in [('mirror_x','水平镜像'),('mirror_y','垂直镜像')]:
            control=QCheckBox();control.setChecked(getattr(self.alignment,key));form.addRow(label,control);fields[key]=control
        note=QLabel('不测距：固定安装，四点拟合，第五点独立核验。\n'
            '多点启用后，上方仅热斑半径有效；改变安装须重做。\n'
            '60°/45°是估计视场；人工对齐不等于精密标定。')
        note.setWordWrap(True);note.setMinimumHeight(note.fontMetrics().lineSpacing()*4);form.addRow(note)
        multi_row=QHBoxLayout();multi=QPushButton('多点对齐（4点＋1点核验）');clear_multi=QPushButton('清除多点对齐')
        multi_row.addWidget(multi);multi_row.addWidget(clear_multi);form.addRow(multi_row)
        def begin_multi():dialog.reject();QTimer.singleShot(0,self._begin_multi_alignment)
        multi.clicked.connect(begin_multi);clear_multi.clicked.connect(self._clear_multi_alignment)
        def current():return PreviewAlignment(**{k:(v.isChecked() if k.startswith('mirror_') else v.value()) for k,v in fields.items()})
        controls=QHBoxLayout();load=QPushButton('读取配置');save=QPushButton('保存配置');preview=QPushButton('预览参数')
        controls.addWidget(load);controls.addWidget(save);controls.addWidget(preview);form.addRow(controls)
        def save_file():
            path,_=QFileDialog.getSaveFileName(dialog,'保存粗对齐配置','approximate_alignment.json','JSON (*.json)')
            if path:
                try:
                    Path(path).write_text(json.dumps(profile_dict(current()),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                except (ValueError,OSError) as error:note.setText('参数未保存：'+str(error))
        def load_file():
            path,_=QFileDialog.getOpenFileName(dialog,'读取粗对齐配置','','JSON (*.json)')
            if not path:return
            try:
                alignment=load_profile(json.loads(Path(path).read_text(encoding='utf-8')))
                for key,control in fields.items():
                    if key.startswith('mirror_'):control.setChecked(getattr(alignment,key))
                    else:control.setValue(getattr(alignment,key))
            except (ValueError,OSError) as exc:note.setText('配置未加载：'+str(exc))
        def preview_settings():
            try:
                self.alignment=current();self.alignment.validate();self._overlay_binding=None;self._update_overlay()
            except ValueError as error:note.setText('参数未应用：'+str(error))
        save.clicked.connect(save_file);load.clicked.connect(load_file);preview.clicked.connect(preview_settings)
        buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Ok|QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(dialog.accept);buttons.rejected.connect(dialog.reject);form.addRow(buttons)
        if dialog.exec()==QDialog.DialogCode.Accepted:
            self.alignment=current();self.alignment.validate();self._alignment_reference=None
            self._persist_alignment();self._overlay_binding=None;self._update_overlay()
        else:
            self.alignment=original_alignment;self._overlay_binding=None;self._update_overlay()

    def _defaults(self):
        if not self._configured and self._default_addresses:
            self.listen_ip, self.board_ip = self._default_addresses()

    @property
    def video_backend(self):
        return self.usb_receiver if self.usb_receiver is not None else self.receiver

    def _settings(self):
        self._defaults()
        dialog = QDialog(self); dialog.setWindowTitle('视频源设置')
        form = QFormLayout(dialog)
        source = QComboBox()
        source.addItem('电脑USB摄像头（当前方案）', 'usb')
        source.addItem('旧板端UDP视频（基线）', 'udp')
        source.setCurrentIndex(0 if self.source_kind == 'usb' else 1)
        form.addRow('视频来源', source)
        devices = camera_devices()
        device = QComboBox(); device.addItem('请选择USB相机（不自动使用内置相机）', '')
        for item in devices: device.addItem(item.description(), device_key(item))
        found = device.findData(self.usb_device_id)
        device.setCurrentIndex(max(0, found))
        form.addRow('相机设备', device)
        formats = QComboBox(); form.addRow('驱动可用格式', formats)
        def refresh_formats():
            formats.clear()
            selected = [item for item in devices if device_key(item) == device.currentData()]
            if not selected: return
            values = list(selected[0].videoFormats())
            for value in values: formats.addItem(format_label(value), format_key(value))
            preferred = self.usb_format_key
            if preferred is None and values: preferred = format_key(preferred_format(values))
            choice = formats.findData(preferred)
            if choice >= 0: formats.setCurrentIndex(choice)
        device.currentIndexChanged.connect(refresh_formats); refresh_formats()
        listen, board = QLineEdit(self.listen_ip), QLineEdit(self.board_ip)
        port = QSpinBox(); port.setRange(1, 65535); port.setValue(self.port)
        form.addRow('旧UDP本机地址', listen); form.addRow('旧UDP板卡地址', board)
        form.addRow('旧UDP端口', port)
        def source_changed():
            usb = source.currentData() == 'usb'
            device.setEnabled(usb); formats.setEnabled(usb)
            for item in (listen, board, port): item.setEnabled(not usb)
        source.currentIndexChanged.connect(source_changed); source_changed()
        note = QLabel('USB直连电脑；先关闭VideoCap，避免相机被占用。优先选择1280×720、30fps。'
                      '格式来自系统驱动；实际帧率、清晰度及声像同步分别核验。')
        note.setWordWrap(True); form.addRow(note)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(dialog.accept); buttons.rejected.connect(dialog.reject); form.addRow(buttons)
        if dialog.exec() != QDialog.DialogCode.Accepted: return
        try:
            kind = source.currentData()
            if kind == 'usb':
                self.usb_device_id = device.currentData()
                self.usb_format_key = formats.currentData()
                self.state.setText('USB相机已选择，点击打开摄像头' if self.usb_device_id else 'USB相机待选择')
                self._save_usb_selection()
            else:
                config = VideoReceiver(listen.text().strip(), port.value(), board.text().strip())
                self.listen_ip, self.board_ip, self.port = config.listen_ip, config.board_ip, config.port
                self._configured = True
                self.state.setText('旧板端UDP基线，点击打开视频')
            self.source_kind = kind
        except ValueError as exc:
            self.state.setText(str(exc))

    def start_selected_video(self):
        if self.source_kind == 'usb': return self.start_usb_video()
        return self.start_video()

    def start_usb_video(self):
        if self.video_backend is not None: raise RuntimeError('先停止原视频会话')
        receiver = self._usb_factory(self)
        try:
            receiver.start(self.usb_device_id, self.usb_format_key)
        except Exception:
            receiver.close(); receiver.deleteLater()
            raise
        self.usb_receiver = receiver
        self._save_usb_selection()
        self.last_frame = None; self.canvas.image = QImage(); self.canvas.markers = ()
        self._overlay_binding = None
        self.canvas.message = '等待USB相机首帧'; self.canvas.update()
        self.state.setText('正在打开USB相机：' + receiver.description)
        self.listen_button.setText('关闭摄像头'); self.settings_button.setEnabled(False)
        return True

    def start_video(self, listen_ip=None, port=None, board_ip=None):
        if self.video_backend is not None:
            raise RuntimeError('先停止原视频会话')
        self._defaults()
        receiver = VideoReceiver(listen_ip if listen_ip is not None else self.listen_ip,
                                 port if port is not None else self.port,
                                 board_ip if board_ip is not None else self.board_ip)
        bound_port = receiver.start()
        self.receiver = receiver
        self.last_frame = None
        self.canvas.image = QImage()
        self.canvas.message = '等待板端视频 · 尚未收到完整帧'
        self.canvas.update()
        self.state.setText(f'监听 {receiver.listen_ip}:{bound_port}')
        self.listen_button.setText('停止视频')
        self.settings_button.setEnabled(False)
        return bound_port

    def _toggle(self):
        if self.video_backend is None:
            try:
                if self.source_kind == 'usb' and not self.usb_device_id:
                    self._settings()
                    if not self.usb_device_id:return
                self.start_selected_video()
            except (ValueError, OSError, RuntimeError) as exc:
                self.state.setText(f'视频未打开：{exc}')
        else:
            self.shutdown()

    def _poll(self):
        receiver = self.video_backend
        if receiver is None:
            return
        if self._stopping:
            self.shutdown()
            return
        frame = receiver.take_latest()
        if frame is not None:
            self._restore_alignment_for_frame(frame)
            if receiver is self.usb_receiver:
                self.canvas.image = frame.image
            else:
                pixels = to_rgb888(frame)
                self.canvas.image = QImage(pixels.data, frame.meta.width, frame.meta.height,
                                           frame.meta.width*3, QImage.Format.Format_RGB888).copy()
            self.last_frame = frame
            self.canvas.message = '图像已接收 · 声源叠加待时间与坐标核验'
            self.detail.setText(f'{frame.meta.width}×{frame.meta.height} · 帧 {frame.meta.frame_id} · '
                f'会话 {frame.meta.epoch} · 时间核验：'
                f'{"发送端声明已核验，仍需声像匹配" if frame.meta.flags & 1 else "未核验"}')
            self.canvas.update()
        self._update_overlay()
        stats = receiver.snapshot()
        if receiver is self.usb_receiver and frame is not None:
            fps = stats['fps']
            rate = f'{fps:.1f}fps' if fps is not None else '帧率待测'
            self.state.setText(f'USB视频已到达 · {rate} · 预览替换{stats["replaced"]}帧')
            self.detail.setText(f'{frame.meta.width}×{frame.meta.height} · USB帧{frame.meta.frame_id} · '
                f'本机到达{rate} · '+self._alignment_note+'；声像坐标与同步尚未实测核验。')
        if stats['error'] or not stats['running']:
            error = stats['error'] or '接收线程已结束'
            self.shutdown()
            self.state.setText(f'视频停止：{error}')
        elif self.last_frame is not None and time.monotonic()-self.last_frame.received_monotonic_s > 1.0:
            self.canvas.message = '视频已过期 · 不作为当前场景'
            self.canvas.update()
        elif self._multi_points is not None:self._multi_instruction()

    def shutdown(self):
        if self.usb_receiver is not None:
            self.usb_receiver.close()
            self.usb_receiver.deleteLater()
            self.usb_receiver = None
        self._stopping = self.receiver is not None
        if self.receiver is not None and not self.receiver.close(timeout=0):
            self.listen_button.setEnabled(False)
            self.state.setText('正在停止视频…')
            return False
        self.receiver = None
        self._stopping = False
        self.last_frame = None
        self._multi_points=None;self._multi_candidate=None;self._multi_error=None;self._recent_directions.clear()
        self.canvas.image = QImage()
        self.canvas.markers=();self.canvas.spatial_overlay=QImage();self._physical_overlay_key=None
        self._overlay_binding=None
        self.canvas.select_point(False);self.point_alignment_button.setText('点选对齐')
        self.canvas.message = '摄像头未打开'
        self.canvas.update()
        self.listen_button.setText('打开摄像头'); self.listen_button.setEnabled(True)
        self.settings_button.setEnabled(True)
        self.state.setText('未监听')
        return True
