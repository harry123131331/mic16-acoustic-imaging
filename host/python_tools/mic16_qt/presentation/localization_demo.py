"""持续定位与电脑USB视频复用同一Qt；相机和板端采集分别手动启停。"""
from __future__ import annotations

import os
import json
from pathlib import Path
import time

from PySide6.QtCore import QEvent
from PySide6.QtWidgets import QApplication, QMenu, QMessageBox, QInputDialog

from mic16_qt.presentation.compact_window import CompactWindow
from mic16_qt.services.control_wire import Capability, DeviceState, Operation
from mic16_qt.services.display_demo_trace import DisplayDemoTrace
from mic16_qt.core.models import SourceEstimate
from mic16_av_fusion.installation_coordinates import installed_angles
from mic16_av_fusion.array_installation import load_vertical_installation,VerticalArrayInstallation,ARRAY_REFERENCE_PLANE
from mic16_av_fusion.array_spherical_coordinates import array_spherical_angles


class LocalizationDemoApplication(QApplication):
    observed_window = None

    def notify(self, receiver, event):
        window = self.observed_window
        candidate = None
        video_candidate=None
        if window is not None and event.type() == QEvent.Type.Paint:
            try:
                if receiver is window.direction.plot.viewport() and window._fast_display_mode:
                    candidate = window.direction._sources_key
                if receiver is window.video_panel.canvas:
                    video_candidate=window.video_panel.overlay_snapshot()
            except RuntimeError:
                pass
        result = super().notify(receiver, event)
        if candidate is not None:
            try:
                if window.direction._sources_key == candidate:
                    if window._last_trace_paint != candidate:
                        window._last_trace_paint = candidate
                        window.demo_trace.add('direction_painted', key=list(candidate),
                                              heatmap_visible=window.direction.image.isVisible())
                    if (window.direction.image.isVisible() and window.direction._heatmap_key == candidate
                            and window._last_trace_heatmap != candidate):
                        window._last_trace_heatmap = candidate
                        window.demo_trace.add('heatmap_painted', key=list(candidate),
                                              response_received_ns=round(window._last_heatmap.received_monotonic_s * 1e9))
            except RuntimeError:
                pass
        if video_candidate is not None:
            try:
                if window.video_panel.overlay_snapshot()==video_candidate:
                    key=tuple(video_candidate['key'])
                    if window._last_trace_video!=key:
                        window._last_trace_video=key
                        window.demo_trace.add('video_overlay_painted',**video_candidate)
            except RuntimeError:
                pass
        return result


class LocalizationDemoWindow(CompactWindow):
    def __init__(self, *, trace_root=None, installation_path=None, **kwargs):
        super().__init__(**kwargs)
        root = Path(__file__).resolve().parents[4] / 'build/board/localization_demo_trace'
        self.demo_trace = DisplayDemoTrace(trace_root or os.environ.get('MIC16_DEMO_TRACE_ROOT', root))
        self._last_trace_paint = self._last_trace_heatmap = self._trace_control = self._trace_raw = None
        self._last_trace_video=None
        self._last_installed_presentation=None
        self._installation_path=(Path(installation_path) if installation_path is not None else
            Path(__file__).resolve().parents[4]/'build/config/array_installation.json')
        self._installation_error=None
        try:self._array_installation=load_vertical_installation(self._installation_path)
        except (OSError,ValueError,TypeError,KeyError) as error:
            self._array_installation=VerticalArrayInstallation();self._installation_error=str(error)
        self._installation_roll_deg=self._array_installation.roll_deg
        self.direction.set_installation_roll(self._installation_roll_deg)
        self.direction.set_array_plane_mode(self._array_installation.reference_plane==ARRAY_REFERENCE_PLANE)
        self.demo_trace.add('array_installation_loaded',reference_plane=self._array_installation.reference_plane,
            roll_deg=self._installation_roll_deg,user_confirmed=self._array_installation.user_confirmed,
            pose_measured=False,error=self._installation_error,camera_independent=True)
        self._heatmap_port = 5003
        QApplication.instance().aboutToQuit.connect(self._save_trace_at_exit)

    def _build_ui(self):
        super()._build_ui()
        self.source_combo.blockSignals(True)
        self.source_combo.clear()
        self.source_combo.addItem('板卡声源定位', 'source_udp')
        self.source_combo.blockSignals(False)
        self.primary_tabs.setTabVisible(self.primary_tabs.indexOf(self.video_panel), True)
        self.primary_tabs.setCurrentIndex(0)
        self.primary_tabs.setTabText(0,'定位方向')
        self.direction.set_point_display_only(True)
        self.source_kind_combo.clear()
        self.source_kind_combo.addItem('原始PCM', 'RAW')
        self.source_kind_combo.hide()
        # 当前演示只提供日常操作，并明确保留QMenu的Qt父对象及Python引用。
        self.menuBar().clear()
        self._demo_menus = [QMenu(title, self) for title in ('文件', '查看', '帮助')]
        for menu in self._demo_menus:
            self.menuBar().addMenu(menu)
        file_menu, view_menu, help_menu = self._demo_menus
        self._action(file_menu, '保存定位结果 CSV…', self.choose_result_export)
        self._action(file_menu, '退出', self.close)
        self._action(view_menu, '运行日志', self.show_log)
        self._action(view_menu, '导出本次运行记录…', self._export_trace)
        self._action(view_menu, '标记测试阶段…', self._mark_validation_phase)
        self._action(help_menu, '上电后如何使用', self.show_power_on_help)
        self._action(help_menu, '角度与阵列安装说明', self.show_installation_help)
        self.record_button.hide()
        self.stop_record_button.hide()
        self.source_count_label.setText('当前方向')
        self.source_table.setColumnHidden(3, True)
        self.heatmap_scale.blockSignals(True)
        self.heatmap_scale.insertItem(0,'色标 自动',None)
        self.heatmap_scale.setCurrentIndex(0)
        self.heatmap_scale.blockSignals(False)
        self.heatmap_scale.setToolTip('自动模式在强响应超过原色标时扩大上限，避免大片饱和红；色条保留实际响应数值。')
        self.direction.response_auto_scale=True
        self.heatmap_scale.hide()
        self.heatmap_button.hide()
        self.notice.setText('加载持续定位固件后，点击“连接”，再点击“开始定位”；USB画面在“摄像头视频”页独立打开。')

    def _refresh_controls(self):
        super()._refresh_controls()
        self.source_kind_combo.hide()
        status = self._device_control.status
        required = int(Capability.FAST_DISPLAY | Capability.CONTINUOUS)
        if status is not None and status.capabilities & required != required:
            self.start_button.setEnabled(False)
            self.start_button.setText('开始定位')
            self.start_button.setToolTip('当前板端程序不是持续定位固件，请先加载配套候选。')

    def _clear_views(self):
        super()._clear_views()
        self.source_count_label.setText('当前方向')
        self.source_table.setColumnHidden(3, True)

    def _open_source_udp(self, *args, **kwargs):
        opened = super()._open_source_udp(*args, **kwargs)
        if opened:
            # 原接收入口会清通道表；本模式已选RAW，不能再依赖增强→RAW切换来恢复通道。
            self.channel_combo.blockSignals(True)
            self.channel_combo.clear()
            for channel in range(16):
                self.channel_combo.addItem(f'CH{channel+1:02}', channel)
            self.channel_combo.setCurrentIndex(0)
            self.channel_combo.blockSignals(False)
        return opened

    def connect_device(self):
        """只打开定位、热图、PCM与控制；不自动打开摄像头或发送START。"""
        if not self.start_source_udp(self._listen_ip, self._source_port, self._board_ip):
            return False
        self._device_session = True
        self._connection_started = time.monotonic()
        try:
            if self._heatmap_receiver is None:
                self.start_heatmap(self._listen_ip, self._heatmap_port, self._board_ip)
                self._session_heatmap = True
            self._device_control.open(self._listen_ip, self._board_ip)
        except (ValueError, OSError, RuntimeError) as error:
            self._source_error = self._connection_error(error)
            self.notice.setText('定位连接未完成：' + self._source_error)
            self._log(self.notice.text())
            self._refresh_controls()
            return False
        self._device_state_changed()
        return True

    def _device_state_changed(self):
        super()._device_state_changed()
        if not self._device_session:
            return
        control = self._device_control
        status = control.status
        self.connection_caption.setText('定位接收' + ('就绪' if self._source_ready else '正在打开') + ' · ' + control.message)
        if status is not None:
            identity = (status.boot, status.epoch, int(status.state), status.code, status.window, status.queued_windows)
            trace = getattr(self, 'demo_trace', None)
            if trace is not None and identity != self._trace_control:
                self._trace_control = identity
                trace.add('control', boot=status.boot, epoch=status.epoch, state=int(status.state),
                          code=status.code, window=status.window, queued_items=status.queued_windows,
                          compute_us=status.compute_us)
            required = int(Capability.FAST_DISPLAY | Capability.CONTINUOUS)
            if status.capabilities & required != required:
                self.notice.setText('当前固件模式不匹配。请运行工程根目录 load_localization_demo.cmd，再连接软件。')
                self._badge('固件模式不匹配')
            elif status.state == DeviceState.FAULT:
                self._fast_current = self._last_heatmap = None
                self.direction.clear(f'定位已停止：状态码{status.code}')
                self.source_table.setRowCount(0)
                self.source_count.setText('—')
                self.result_state.setText('本次定位已中止')
                self.audio_state.setText('采集已停止；已有波形仅供查看')

    def _installed_roll(self):
        # 声学水平基准只来自阵列安装记录；相机开关、换设备或改投影都不得改角度。
        return self._installation_roll_deg

    def _present_installed_direction(self,frame):
        from mic16_av_fusion.hemisphere_grid import grid_geometry,HEMISPHERE_GRID
        geometry=grid_geometry(frame)
        roll=self._installed_roll();self.direction.set_installation_roll(roll)
        self.direction.set_scan_geometry(geometry)
        installation=self._array_installation
        plane=installation.reference_plane==ARRAY_REFERENCE_PLANE
        if plane:
            az,el=array_spherical_angles(frame.azimuth_deg,frame.elevation_deg,roll)
            defined=az is not None
            if not defined:az=0.
            definition='MIC12方位0°、MIC0为90°、MIC4为180°、MIC8为270°；俯仰相对阵列平面，正前方90°。'
        else:
            az,el=installed_angles(frame.azimuth_deg,frame.elevation_deg,roll);defined=True
            definition=f'历史水平基准，上正下负；安装旋转{roll:g}°。'
        boundary=bool(frame.map_valid and (frame.grid//25==0 if geometry==HEMISPHERE_GRID else (frame.grid%25 in (0,24) or frame.grid//25 in (0,12))))
        resolution=('方位15°、板面俯仰7.5°；0/360接缝和法线为重复射线' if geometry==HEMISPHERE_GRID else '原始5°有限扫描网格')
        evidence=(definition+f'安装由用户确认：{installation.user_confirmed}；未作姿态精测。'
            f'原始方向{frame.azimuth_deg:+.1f}°/{frame.elevation_deg:+.1f}°；'
            f'扫描边界：{boundary}；{resolution}，显示小数不代表0.1°精度。')
        sources=(SourceEstimate('S1',az,el,0.,'CURRENT',evidence),) if frame.valid else ()
        self.direction.show_result(sources,frame.key)
        state=self.result_state.text();self._show_sources(sources,frame.sequence);self.result_state.setText(state)
        if plane and sources:
            self.source_table.item(0,1).setText(f'{az:.1f}°')
            self.source_table.item(0,2).setText(f'{el:.1f}°')
        if sources and not defined:
            self.source_table.item(0,1).setText('—')
            self.source_table.item(0,1).setToolTip('正对阵列法线时，板面内投影为零，方位角未定义。')
        if not installation.user_confirmed and '阵列安装待确认' not in state:
            self.result_state.setText(state+' · 阵列安装待确认')
        if frame.valid and boundary and '扫描边界' not in self.result_state.text():
            self.result_state.setText(self.result_state.text()+' · 方向位于扫描边界')
        identity=(frame.key,roll)
        if identity!=self._last_installed_presentation:
            self._last_installed_presentation=identity
            self.demo_trace.add('installed_direction_presented',key=list(frame.key),roll_deg=roll,
                raw_azimuth_deg=frame.azimuth_deg,raw_elevation_deg=frame.elevation_deg,
                azimuth_deg=az,elevation_deg=el,valid=frame.valid,physical_frame=installation.coordinate_frame,
                reference_plane=installation.reference_plane,installation_user_confirmed=installation.user_confirmed,
                installation_pose_measured=installation.pose_measured,azimuth_defined=defined,search_boundary=boundary,scan_geometry=geometry,
                main_display='DIRECTION_POINT' if self.direction.point_display_only else 'SPATIAL_RESPONSE',
                color_scale_mode=('NOT_DISPLAYED' if self.direction.point_display_only else ('AUTOMATIC_UPPER_LIMIT' if self.direction.response_auto_scale else 'FIXED_MANUAL')),
                display_color_levels=(list(self.direction.display_levels) if self.direction._heatmap_key==frame.key and self.direction.image.isVisible() else None))
        if self._history and self._history[-1].get('frame_index')==frame.sequence:
            self._history[-1]['installed_sources']=sources
            self._history[-1]['installation_roll_deg']=roll
            self._history[-1]['angle_reference_plane']=installation.reference_plane
            self._history[-1]['installation_user_confirmed']=installation.user_confirmed
            self._history[-1]['installation_pose_measured']=installation.pose_measured
            self._history[-1]['azimuth_defined']=defined
            if plane:self._history[-1]['coordinate_frame']=installation.coordinate_frame

    def _apply_fast_map(self):
        self.direction.set_installation_roll(self._installed_roll())
        super()._apply_fast_map()
        if self._fast_current is not None and 0<=time.monotonic()-self._fast_current.received_monotonic_s<1.:
            self._present_installed_direction(self._fast_current)
            if self.direction.array_plane_mode:
                age=max(0.,time.monotonic()-self._fast_current.received_monotonic_s)
                self.map_caption.setText(f'定位方向 · 窗{self._fast_current.sequence} · 接收后{age:.1f}s')

    def _apply_fast_direction(self, frame):
        self.direction.set_installation_roll(self._installed_roll())
        accepted = super()._apply_fast_direction(frame)
        if accepted:
            self.demo_trace.direction(frame)
        return accepted

    def _refresh(self):
        if hasattr(self,'_installation_roll_deg') and self._fast_display_mode:
            roll=self._installed_roll()
            if self.direction.installation_roll_deg!=roll:
                self._apply_fast_map()
        super()._refresh()

    def _refresh_source_udp(self):
        super()._refresh_source_udp()
        status = self._device_control.status
        if status is not None and status.state == DeviceState.FAULT:
            # 无结果时旧接收刷新会写“等待板端”，不能覆盖已经确认的故障状态。
            self._badge('板端故障 · 本时段已停止')
        worker = self._source_udp_worker
        if worker is not None:
            raw = worker.raw_last
            identity = (id(worker), worker.raw_completed, worker.display_dropped, worker.ignored_old_epoch)
            if identity != self._trace_raw:
                self._trace_raw = identity
                self.demo_trace.add('receiver', worker=id(worker), raw_completed=worker.raw_completed,
                                    raw_epoch=None if raw is None else raw.epoch,
                                    raw_frame=None if raw is None else raw.frame,
                                    display_rejected=worker.display_dropped, ignored_epoch=worker.ignored_old_epoch,
                                    raw_boot=worker.display_boot,display_coverage=worker.display_coverage,receive_error=self._source_error)

    def _start_device(self):
        self._refresh_controls()
        if not self.start_button.isEnabled():
            self.notice.setText('请先连接并确认持续定位固件已就绪。')
            return
        super()._start_device()
        if self._device_control.pending is not None:
            self.demo_trace.add('start_request', request=self._device_control.pending['request'])

    def _stop_device(self):
        super()._stop_device()
        if self._device_control.pending is not None and self._device_control.pending['operation'] == Operation.STOP:
            self.demo_trace.add('stop_request', request=self._device_control.pending['request'])

    def _export_trace(self):
        try:
            path = self.demo_trace.save()
            self.notice.setText('运行记录已保存：' + str(path))
        except OSError as error:
            self.notice.setText('运行记录保存失败：' + str(error))

    def _mark_validation_phase(self):
        label,accepted=QInputDialog.getItem(self,'标记测试阶段','仅作为手动分段，不作为角度真值或精确事件时刻',
            ['静态：中间','静态：左侧','静态：右侧','移动：左到右','移动：右到左','静音','声像对齐检查'],0,True)
        if accepted:
            try:
                self.demo_trace.mark_phase(label)
                self.notice.setText('已记录测试阶段：'+label.strip())
            except ValueError as error:self.notice.setText(str(error))

    def _save_trace_at_exit(self):
        try:
            self.demo_trace.save()
        except OSError as error:
            print('运行记录保存失败：' + str(error))

    def show_power_on_help(self):
        QMessageBox.information(self, '上电后如何使用',
            '1. 首次上电双击工程根目录 load_localization_demo.cmd，加载当前前半球版本并打开软件。\n'
            '2. 打开本软件，点击“连接”，等待板端已就绪。\n'
            '3. 点击“开始定位”，定位页的小圆点随新方向更新。\n'
            '4. 点击“停止定位”，等板端已就绪后可再次开始。\n'
            '5. 摄像头USB直连电脑，在“摄像头视频→视频设置”选择USB Camera，再点击“打开摄像头”。\n'
            '6. 勾选“叠加声源热图”，收到有效定位和真实空间响应后显示；无效或过期时隐藏。\n'
            '7. 固定声源后可用“点选对齐”点击画面中的实际声源；只调显示偏移，参数随相机画幅保存。\n'
            '同次上电重复开始不用重开Vitis或重新下载。断电后临时固件需重新加载。\n'
            'VideoCap需先关闭，避免占用相机；首次声像对齐在“对齐设置”完成，画面出现不代表坐标已标定。\n'
            '运行记录在“查看→导出本次运行记录”，退出软件时也会保存。')

    def show_installation_help(self):
        installation=self._array_installation;mic=installation.cardinal_microphones
        confirmation='已由用户确认竖直安装' if installation.user_confirmed else '安装记录未确认，请核对实际摆放'
        definition=('方位：右侧MIC12为0°，上方MIC0为90°，左侧MIC4为180°，下方MIC8为270°。\n'
            '俯仰：阵列PCB平面为0°，正前方为90°。正对中心时方位显示“—”。\n'
            '定位页只显示小圆点；前半球版本图表与右侧表使用相同阵列球坐标，旧有限网格保留偏角轴。\n'
            '扫描范围外仅显示深蓝底色，没有补造响应。\n'
            if installation.reference_plane==ARRAY_REFERENCE_PLANE else
            '历史角度定义：阵列中心水平面为0°，上正下负。\n')
        QMessageBox.information(self,'角度与阵列安装说明',definition+
            f'正对阵列正面看：上MIC{mic["up"]}、左MIC{mic["left"]}、下MIC{mic["down"]}、右MIC{mic["right"]}。\n'
            f'{confirmation}；法线应朝向水平，未作姿态精测。\n'
            '相机镜像、视场、像素偏移与多点对齐只影响画面，不改变声学角度。\n'
            '阵列倾斜或编号朝向改变后，需重新确认安装关系。')
