"""快速演示的离线对齐、动态失效和实际Qt绘制核验；不打开硬件或端口。"""
from dataclasses import replace
from pathlib import Path
import json
import os
import sys
import tempfile
import time
import unittest
import zlib
import threading
import csv

os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host/python_tools'))
import numpy as np
from PySide6.QtCore import QPoint,Qt,QTimer
from PySide6.QtGui import QImage,QColor,QFont
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication,QDialog,QDoubleSpinBox,QPushButton,QFormLayout,QLabel
from mic16_av_fusion.preview_projection import PreviewAlignment,project_direction,align_direction_to_point,profile_dict,load_profile
from mic16_av_fusion.spatial_response_overlay import response_rgba
from mic16_av_fusion.preview_alignment_store import video_binding,save_bound_alignment,load_bound_alignment
from mic16_av_fusion.fast_display_wire import FastDisplayFrame,FastResponseFrame
from mic16_qt.presentation.usb_camera_preview import UsbVideoMeta,UsbPreviewFrame
from mic16_qt.presentation.video_panel import VideoPanel,VideoCanvas
from mic16_qt.presentation.localization_demo import LocalizationDemoApplication,LocalizationDemoWindow
from mic16_qt.services.display_demo_trace import DisplayDemoTrace
from mic16_qt.services.raw_pcm_wire import HEADER as PCM_HEADER,decode as decode_pcm,RawPcmAssembler
from mic16_qt.app import _install_cjk_font
from mic16_qt.presentation.compact_theme import compact_stylesheet
from mic16_av_fusion.installation_coordinates import installed_angles,installed_response_grid
from mic16_qt.services.source_network import SourceNetworkWorker
from mic16_av_fusion.heatmap_receiver import HeatmapReceiver
from mic16_av_fusion.bearing_image_alignment import (
    BearingImageMap,fit_bearing_image_map,check_holdout,save_bearing_alignment,load_bearing_alignment,
)


def video_frame(*,device='offline-camera-a',width=1280,height=720,sequence=1,rotation=(0,False)):
    image=QImage(width,height,QImage.Format.Format_RGB32);image.fill(QColor('#596975'))
    meta=UsbVideoMeta(width,height,sequence,1,device,(width,height,'offline-fixture',30.,30.),rotation)
    return UsbPreviewFrame(image,meta,time.monotonic(),-1)


def observations(sequence=0,az=0.,el=0.,valid=True):
    grid=int((el+30)/5)*25+int((az+60)/5)
    base=dict(boot=17,epoch=1,sequence=sequence,first_sample=sequence*8192,end_sample=(sequence+1)*8192,
        completed_tick=1000+sequence*1000,tick_hz=1000,first_received_tick=sequence*1000,
        last_front_tick=900+sequence*1000,valid=valid,map_valid=True,reason=0 if valid else 1,
        grid=grid,received_monotonic_s=time.monotonic(),wire_version=3)
    values=np.full((13,25),.12,dtype=np.float32);values.reshape(-1)[grid]=.35;values.setflags(write=False)
    direction=FastDisplayFrame(**base,azimuth_deg=az,elevation_deg=el,coherence=.35,contrast=.1,
                               support_fraction=1.,values=None)
    response=FastResponseFrame(**base,values=values)
    return direction,response


class FastDemoAlignmentChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=QApplication.instance() or LocalizationDemoApplication([])

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mic16_alignment_')
        assert Path(self.temp.name).resolve().parent==Path(tempfile.gettempdir()).resolve()
        assert Path(self.temp.name).name.startswith('mic16_alignment_')
        self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name)
        self.panels=[]

    def tearDown(self):
        self.app.observed_window=None
        for panel in self.panels:
            panel.shutdown();panel.close();panel.deleteLater()
        self.app.processEvents()

    def panel(self):
        panel=VideoPanel(settings_path=self.folder/'usb_camera_preview.json')
        self.panels.append(panel)
        return panel

    def attach(self,panel,frame=None,pair=None):
        frame=frame or video_frame()
        panel.last_frame=frame;panel.canvas.image=frame.image
        panel._restore_alignment_for_frame(frame)
        panel.overlay_enabled.setChecked(True)
        if pair is not None:panel.set_spatial_response(*pair)
        return frame

    def test_point_shift_matches_forward_projection_and_keeps_direction(self):
        for mx in (False,True):
            for my in (False,True):
                old=PreviewAlignment(mirror_x=mx,mirror_y=my)
                changed=align_direction_to_point(10.,5.,.6,.55,old)
                projected=project_direction(10.,5.,changed)
                self.assertAlmostEqual(projected[0],.6)
                self.assertAlmostEqual(projected[1],.55)
                self.assertEqual(changed.yaw_deg,old.yaw_deg)
                self.assertEqual(changed.horizontal_fov_deg,old.horizontal_fov_deg)
        for u,v in [(1.,.5),(float('nan'),.5),(True,.5),(-.1,.5)]:
            with self.assertRaises(ValueError):align_direction_to_point(0.,0.,u,v,PreviewAlignment())
        with self.assertRaisesRegex(ValueError,'视场外'):
            align_direction_to_point(60.,0.,.5,.5,PreviewAlignment())

    def test_profile_binding_roundtrip_and_no_calibration_claim(self):
        path=self.folder/'alignment.json';binding=video_binding(video_frame().meta)
        alignment=PreviewAlignment(yaw_deg=10.,horizontal_fov_deg=90.,offset_v=.05)
        save_bound_alignment(path,alignment,binding,dict(method='OFFLINE_TEST'))
        restored,reference=load_bound_alignment(path,binding)
        self.assertEqual(restored,alignment);self.assertEqual(reference['method'],'OFFLINE_TEST')
        doc=json.loads(path.read_text(encoding='utf-8'))
        for field in ('calibrated','physical_AV_sync_verified','distance_estimated','intrinsics_measured'):
            self.assertIs(doc[field],False)
        for frame in [video_frame(device='different'),video_frame(width=640,height=480),video_frame(rotation=(0,True))]:
            with self.assertRaisesRegex(ValueError,'不匹配'):load_bound_alignment(path,video_binding(frame.meta))
        doc['calibrated']=True;path.write_text(json.dumps(doc),encoding='utf-8')
        with self.assertRaises(ValueError):load_bound_alignment(path,binding)
        self.assertEqual(list(self.folder.glob('*.tmp')),[])

    def test_reopen_restores_only_same_camera_geometry(self):
        first=self.panel();self.attach(first)
        expected=PreviewAlignment(yaw_deg=7.,horizontal_fov_deg=92.,offset_u=.04)
        first.alignment=expected;self.assertTrue(first._persist_alignment())
        second=self.panel();self.attach(second)
        self.assertEqual(second.alignment,expected)
        self.attach(second,video_frame(device='another-camera'))
        self.assertEqual(second.alignment,PreviewAlignment())
        self.assertIn('不匹配',second._alignment_note)
        self.assertIsNone(second.video_backend)

    def test_point_selection_uses_visible_image_not_letterbox(self):
        canvas=VideoCanvas();canvas.resize(640,640);canvas.image=video_frame().image
        canvas.select_point(True);received=[]
        canvas.point_selected.connect(lambda u,v:received.append((u,v)))
        canvas.show();self.app.processEvents()
        QTest.mouseClick(canvas,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,QPoint(320,10))
        self.assertEqual(received,[])
        rect=canvas.image_rect();self.assertGreater(rect.top(),0)
        QTest.mouseClick(canvas,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,rect.center().toPoint())
        self.assertAlmostEqual(received[-1][0],.5,delta=1/rect.width())
        self.assertAlmostEqual(received[-1][1],.5,delta=1/rect.height())
        canvas.close();canvas.deleteLater()

    def test_valid_point_aligns_and_records_actual_observation_identity(self):
        panel=self.panel();pair=observations(0,10.,5.);self.attach(panel,pair=pair)
        panel.resize(1200,800);panel.show();self.app.processEvents()
        panel._begin_point_alignment();self.assertTrue(panel.canvas.point_selection)
        rect=panel.canvas.image_rect();target=QPoint(round(rect.left()+.6*rect.width()),round(rect.top()+.55*rect.height()))
        QTest.mouseClick(panel.canvas,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,target)
        self.assertFalse(panel.canvas.point_selection)
        self.assertIs(panel._physical_response[0],pair[0])
        reference=panel._alignment_reference
        self.assertEqual(reference['acoustic_key'],list(pair[0].key))
        point=project_direction(10.,5.,panel.alignment)
        self.assertAlmostEqual(point[0],reference['u']);self.assertAlmostEqual(point[1],reference['v'])
        self.assertTrue(panel._alignment_path.exists())
        self.assertFalse(reference['physical_sync_verified'])

    def test_stale_invalid_and_missing_responses_cannot_align_or_paint(self):
        panel=self.panel();pair=observations();self.attach(panel,pair=pair)
        before=panel.alignment
        old=tuple(replace(value,received_monotonic_s=time.monotonic()-2.) for value in pair)
        panel.set_spatial_response(*old);panel._align_to_video_point(.6,.5)
        self.assertEqual(panel.alignment,before);self.assertFalse(panel._alignment_path.exists())
        self.assertIsNone(panel.overlay_snapshot());self.assertTrue(panel.canvas.spatial_overlay.isNull())
        fresh=observations(1)
        panel.set_spatial_response(fresh[0],replace(fresh[1],received_monotonic_s=time.monotonic()-2.))
        self.assertTrue(panel.canvas.spatial_overlay.isNull())
        panel.set_spatial_response(*observations(1,valid=False))
        self.assertIsNone(panel.overlay_snapshot());self.assertTrue(panel.canvas.spatial_overlay.isNull())
        panel.clear_spatial_response();panel._begin_point_alignment()
        self.assertFalse(panel.canvas.point_selection)

    def test_mode_change_hides_old_projection_and_does_not_reuse_saved_offset(self):
        panel=self.panel();self.attach(panel,pair=observations())
        panel.alignment=PreviewAlignment(offset_u=.15);panel._persist_alignment();panel._update_overlay()
        self.assertFalse(panel.canvas.spatial_overlay.isNull())
        different=video_frame(width=640,height=480)
        self.attach(panel,different);panel._update_overlay()
        self.assertEqual(panel.alignment,PreviewAlignment())
        self.assertTrue(panel.canvas.spatial_overlay.isNull())

    def test_cancel_restores_parameters_after_live_preview(self):
        panel=self.panel();self.attach(panel,pair=observations())
        original=panel.alignment;observed=[]
        def interact():
            dialog=self.app.activeModalWidget();self.assertIsInstance(dialog,QDialog)
            dialog.findChildren(QDoubleSpinBox)[0].setValue(12.)
            next(button for button in dialog.findChildren(QPushButton) if button.text()=='预览参数').click()
            observed.append(panel.alignment.yaw_deg);dialog.reject()
        QTimer.singleShot(0,interact);panel._alignment_settings()
        self.assertEqual(observed,[12.]);self.assertEqual(panel.alignment,original)
        self.assertFalse(panel._alignment_path.exists())

    def test_qt_dynamic_updates_and_video_overlay_paint_trace(self):
        window=LocalizationDemoWindow(trace_root=self.folder/'trace')
        self.addCleanup(window.close)
        self.app.observed_window=window
        window.resize(1380,900);window.show();self.app.processEvents()
        window._timer.stop();window.video_panel._timer.stop()
        panel=window.video_panel;panel._settings_path=self.folder/'usb_camera_preview.json'
        panel._alignment_path=self.folder/'preview_alignment.json'
        window.primary_tabs.setCurrentWidget(panel);window._fast_display_mode=True
        window.notice.setText('离线Qt验证夹具，非板卡或现场测量')
        positions=[]
        for sequence,az in enumerate((-20.,0.,20.)):
            direction,response=observations(sequence,az)
            self.attach(panel,video_frame(sequence=sequence+1))
            self.assertTrue(window._apply_fast_direction(direction))
            window._last_heatmap=response;window._apply_fast_map()
            self.app.processEvents();panel.canvas.repaint();self.app.processEvents()
            self.assertFalse(window._apply_fast_direction(direction))
            positions.append(project_direction(az,0.,panel.alignment)[0])
        self.assertLess(positions[0],positions[1]);self.assertLess(positions[1],positions[2])
        summary=window.demo_trace.summary()
        self.assertEqual(summary['direction_applied_count'],3)
        self.assertEqual(summary['video_overlay_painted_count'],3)
        self.assertIsNone(summary['physical_sound_to_screen_ms'])
        self.assertFalse(summary['realtime_qualified'])
        out=ROOT/'build/verification/fast_demo_refinement_20261031'
        out.mkdir(parents=True,exist_ok=True)
        window.grab().save(str(out/'offline_video_overlay.png'))
        old=observations(0)[0];self.assertFalse(window._apply_fast_direction(old))
        rejected,response=observations(3,valid=False)
        self.assertTrue(window._apply_fast_direction(rejected));window._last_heatmap=response;window._apply_fast_map()
        panel.canvas.repaint();self.app.processEvents()
        self.assertEqual(window.demo_trace.summary()['video_overlay_painted_count'],3)
        self.assertTrue(panel.canvas.spatial_overlay.isNull())
        window._bind_display_epoch(17,2)
        self.assertFalse(window._apply_fast_direction(rejected))
        self.assertTrue(panel.canvas.spatial_overlay.isNull())
        window.demo_trace.save()

    def test_phase_markers_are_not_physical_ground_truth(self):
        trace=DisplayDemoTrace(self.folder)
        trace.mark_phase('静态：中间')
        for invalid in ('', 'x'*81,None):
            with self.assertRaises(ValueError):trace.mark_phase(invalid)
        marker=trace.summary()['manual_phase_markers'][0]
        self.assertFalse(marker['physical_event_time_verified'])
        self.assertFalse(marker['angular_ground_truth_verified'])

    def test_fully_transparent_response_is_not_counted_as_visible_overlay(self):
        panel=self.panel();direction,response=observations()
        values=np.full((13,25),.2,dtype=np.float32);values.reshape(-1)[direction.grid]=.22
        self.attach(panel,pair=(replace(direction,coherence=.22),replace(response,values=values)))
        self.assertFalse(panel._physical_overlay_has_signal)
        self.assertIn('无可见热图区',panel.canvas.message)
        self.assertIsNone(panel.overlay_snapshot())
        # 用户明确改显示色限仍使用同一真实数组，不逐帧自动归一化或生成斑点。
        panel.set_spatial_response(replace(direction,coherence=.22),replace(response,values=values),levels=(.1,.25))
        self.assertTrue(panel._physical_overlay_has_signal)
        self.assertIsNotNone(panel.overlay_snapshot())

    def test_display_flip_mirrors_video_and_response_together(self):
        canvas=VideoCanvas();canvas.resize(320,180);canvas.message=''
        image=QImage(64,32,QImage.Format.Format_RGB32);image.fill(QColor('red'))
        for y in range(32):
            for x in range(32,64):image.setPixelColor(x,y,QColor('blue'))
        layer=QImage(64,32,QImage.Format.Format_ARGB32);layer.fill(Qt.GlobalColor.transparent)
        for y in range(12,20):
            for x in range(10,18):layer.setPixelColor(x,y,QColor(0,255,0,220))
        canvas.image=image;canvas.spatial_overlay=layer;canvas.show();self.app.processEvents()
        before=canvas.grab().toImage().convertToFormat(QImage.Format.Format_RGBA8888)
        canvas.display_mirror_x=True;canvas.update();self.app.processEvents()
        after=canvas.grab().toImage().convertToFormat(QImage.Format.Format_RGBA8888)
        expected=before.mirrored(True,False)
        actual_pixels=np.frombuffer(after.constBits(),dtype=np.uint8).astype(np.int16)
        expected_pixels=np.frombuffer(expected.constBits(),dtype=np.uint8).astype(np.int16)
        # 反向平滑光栅存在1级颜色舍入；逐像素比较仍会拒绝层间错位或少翻一层。
        self.assertLessEqual(int(np.max(np.abs(actual_pixels-expected_pixels))),1)
        canvas.close();canvas.deleteLater()

    def test_display_flip_point_selection_keeps_base_projection_correspondence(self):
        panel=self.panel();pair=observations(0,10.,5.);self.attach(panel,pair=pair)
        panel.display_flip.setChecked(True)
        panel.resize(1200,800);panel.show();self.app.processEvents();panel._begin_point_alignment()
        rect=panel.canvas.image_rect();target=QPoint(round(rect.left()+.3*rect.width()),round(rect.top()+.55*rect.height()))
        QTest.mouseClick(panel.canvas,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,target)
        reference=panel._alignment_reference;point=project_direction(10.,5.,panel.alignment)
        self.assertAlmostEqual(point[0],reference['u'])
        self.assertAlmostEqual(1.-point[0],.3,delta=1/rect.width())
        self.assertIs(panel._physical_response[0],pair[0])
        self.assertTrue(panel.overlay_snapshot()['display_mirror_x'])

    def test_display_flip_preference_is_restored_without_changing_camera_selection(self):
        first=self.panel();first.usb_device_id=b'fixture-usb'.hex()
        first.usb_format_key=(1280,720,'Format_NV12',30.,30.)
        first.display_flip.setChecked(True)
        second=self.panel()
        self.assertTrue(second.display_mirror_x);self.assertTrue(second.canvas.display_mirror_x)
        self.assertEqual(second.usb_device_id,first.usb_device_id)
        self.assertEqual(second.usb_format_key,first.usb_format_key)

    def test_mount_rotation_cardinal_axes_and_old_profile_compatibility(self):
        profile=PreviewAlignment(roll_deg=90.)
        x=project_direction(10.,0.,profile);y=project_direction(0.,10.,profile)
        self.assertAlmostEqual(x[0],.5);self.assertLess(x[1],.5)
        self.assertLess(y[0],.5);self.assertAlmostEqual(y[1],.5)
        saved=profile_dict(PreviewAlignment());saved['settings'].pop('roll_deg')
        self.assertEqual(load_profile(saved),PreviewAlignment())
        for invalid in (181.,-181.,float('nan'),True):
            with self.assertRaises(ValueError):PreviewAlignment(roll_deg=invalid).validate()

    def test_mount_rotation_response_and_direction_use_the_same_geometry(self):
        # 单网格脉冲仅为坐标夹具；不模拟现场声学精度或生成演示热斑。
        az,el=10.,5.;grid=np.full((13,25),.1);grid[7,14]=.35
        for roll in (-90.,0.,37.,90.):
            for mx in (False,True):
                for my in (False,True):
                    profile=PreviewAlignment(yaw_deg=3.,pitch_deg=-2.,roll_deg=roll,
                        horizontal_fov_deg=90.,vertical_fov_deg=70.,mirror_x=mx,mirror_y=my)
                    point=project_direction(az,el,profile)
                    layer=response_rgba(grid,profile,640,360)
                    row,column=np.unravel_index(np.argmax(layer[:,:,3]),layer.shape[:2])
                    self.assertAlmostEqual((column+.5)/640,point[0],delta=.015)
                    self.assertAlmostEqual((row+.5)/360,point[1],delta=.015)

    def test_mount_rotation_persistence_and_post_mirror_direction(self):
        panel=self.panel();self.attach(panel,pair=observations(0,-5.,-20.))
        panel.alignment=PreviewAlignment(roll_deg=90.,mirror_x=True)
        panel.display_flip.setChecked(True);panel._persist_alignment();panel._update_overlay()
        point=project_direction(-5.,-20.,panel.alignment)
        self.assertGreater(1-point[0],.5);self.assertGreater(point[1],.5)
        self.assertEqual(panel.overlay_snapshot()['alignment_settings']['roll_deg'],90.)
        second=self.panel();self.attach(second)
        self.assertEqual(second.alignment,panel.alignment)

    @staticmethod
    def pcm_packet(value,flags=0,part=0,scale=18):
        payload=np.full((16,16),value,dtype='<f4').tobytes()
        header=PCM_HEADER.pack(b'M16P',1,64,7,0,0,78125,2,16,1,2048,part,128,part*16,16,scale,flags,0)
        checksum=zlib.crc32(header[:60]+payload)
        return header[:60]+checksum.to_bytes(4,'little')+payload

    def test_pcm_headroom_explicit_contract_retains_legacy_and_hard_limits(self):
        self.assertEqual(len(decode_pcm(self.pcm_packet(1.))),6)
        with self.assertRaises(ValueError):decode_pcm(self.pcm_packet(1.00465))
        for value in (-4.,-1.00465,1.00465,4.):
            decoded=decode_pcm(self.pcm_packet(value,2),with_headroom=True)
            self.assertEqual(decoded[-1],2)
            self.assertEqual(decoded[-2][0,0].tobytes(),np.float32(value).tobytes())
        for value,flags,scale in [(4.00001,2,18),(float('nan'),2,18),(float('inf'),2,18),(0.,1,18),(0.,2,19)]:
            with self.assertRaises(ValueError):decode_pcm(self.pcm_packet(value,flags,scale=scale))

    def test_pcm_headroom_cannot_mix_contracts_within_a_frame(self):
        receiver=RawPcmAssembler();receiver.push(self.pcm_packet(.5,2))
        with self.assertRaisesRegex(ValueError,'幅度合同冲突'):receiver.push(self.pcm_packet(.5,0,part=1))
        receiver=RawPcmAssembler();frames=[]
        for part in range(128):frames.extend(receiver.push(self.pcm_packet(1.00465,2,part=part)))
        self.assertEqual(len(frames),1);self.assertEqual(frames[0].headroom_bits,2)
        np.testing.assert_array_equal(frames[0].pcm,np.full((2048,16),1.00465,dtype='<f4'))
        receiver.finish()

    def test_overlay_extent_masks_weaker_response_without_changing_peak_geometry_or_rgb(self):
        # 仅用连续网格夹具核验显示；不作为现场声学结果或演示输入。
        yy,xx=np.mgrid[-30:31:5,-60:61:5]
        values=.1+.25*np.exp(-((xx/18.)**2+(yy/12.)**2))
        frozen=values.copy();alignment=PreviewAlignment(horizontal_fov_deg=120.,vertical_fov_deg=60.)
        layers=[response_rgba(values,alignment,641,361,extent_percent=p) for p in (50,75,100)]
        for small,big in zip(layers,layers[1:]):
            np.testing.assert_array_equal(small[:,:,:3],big[:,:,:3])
            self.assertTrue(np.all((small[:,:,3]>0)<=(big[:,:,3]>0)))
            self.assertLess(np.count_nonzero(small[:,:,3]),np.count_nonzero(big[:,:,3]))
            self.assertEqual(np.unravel_index(np.argmax(small[:,:,3]),small.shape[:2]),
                             np.unravel_index(np.argmax(big[:,:,3]),big.shape[:2]))
            self.assertEqual(small[180,320,3],big[180,320,3])
        np.testing.assert_array_equal(values,frozen)
        for invalid in (0,24,101,True,float('nan')):
            with self.assertRaises(ValueError):response_rgba(values,alignment,extent_percent=invalid)

    def test_overlay_extent_control_repaints_current_response_and_persists_only_display_selection(self):
        panel=self.panel();panel.usb_device_id=b'fixture-usb'.hex()
        pair=observations();self.attach(panel,pair=pair)
        self.assertEqual(panel.overlay_extent.currentText(),'小')
        initial_key=panel._physical_overlay_key
        panel.overlay_extent.setCurrentIndex(panel.overlay_extent.findData(75))
        self.assertEqual(panel.overlay_snapshot()['key'],list(pair[0].key))
        self.assertEqual(panel.overlay_snapshot()['overlay_extent_percent'],75)
        self.assertNotEqual(panel._physical_overlay_key,initial_key)
        self.assertIs(panel._physical_response[1],pair[1])
        second=self.panel()
        self.assertEqual(second.overlay_extent.currentText(),'中')
        self.assertEqual(second.usb_device_id,panel.usb_device_id)
        self.assertIsNone(second.video_backend)

    def test_saturated_plateau_is_bounded_on_camera_without_fabricating_spectrum(self):
        # 饱和平台正是只改透明阈值仍然遮挡画面的反例。
        values=np.full((13,25),.7);before=values.copy();alignment=PreviewAlignment()
        full=response_rgba(values,alignment,640,360)
        for extent in (50,75,100):
            layer=response_rgba(values,alignment,640,360,extent_percent=extent,focus_direction=(0.,0.))
            rows,columns=np.nonzero(layer[:,:,3])
            radius=project_direction(0.,0.,replace(alignment,radius_deg=3.*extent/100.))
            self.assertLessEqual(columns.max()-columns.min()+1,2*radius[2]*640+2)
            self.assertLessEqual(rows.max()-rows.min()+1,2*radius[3]*360+2)
            self.assertGreater(layer[180,320,3],0)
            self.assertLess(layer[180,320,2],layer[180,320,0])
            self.assertTrue(np.any((layer[:,:,2]>layer[:,:,0])&(layer[:,:,3]>0)))
        np.testing.assert_array_equal(values,before)

    def test_camera_roi_uses_accepted_direction_geometry_and_does_not_clamp_offscreen(self):
        values=np.full((13,25),.7)
        alignment=PreviewAlignment(roll_deg=90.,mirror_x=True,offset_u=.03)
        point=project_direction(10.,5.,alignment)
        layer=response_rgba(values,alignment,640,360,extent_percent=50,focus_direction=(10.,5.))
        self.assertGreater(layer[int(point[1]*360),int(point[0]*640),3],0)
        self.assertEqual(layer[0,0,3],0)
        offscreen=response_rgba(values,PreviewAlignment(),640,360,focus_direction=(60.,0.))
        self.assertFalse(offscreen[:,:,3].any())
        with self.assertRaises(ValueError):response_rgba(values,alignment,focus_direction=(float('nan'),0.))

    def test_direction_marker_has_red_yellow_green_blue_gradient_inside_selected_radius(self):
        values=np.full((13,25),.7);before=values.copy();alignment=PreviewAlignment()
        sizes=[]
        for extent in (50,75,100):
            rgba=response_rgba(values,alignment,641,361,extent_percent=extent,focus_direction=(0.,0.))
            visible=rgba[:,:,3]>30;r,g,b=(rgba[:,:,c].astype(int) for c in range(3))
            self.assertGreater(rgba[180,320,0],rgba[180,320,2]+100)
            self.assertTrue(np.any(visible&(r>120)&(g>120)&(b<100)))
            self.assertTrue(np.any(visible&(g>r+20)&(g>b)))
            self.assertTrue(np.any(visible&(b>r+30)&(b>g)))
            point=project_direction(0.,0.,replace(alignment,radius_deg=3.*extent/100.))
            rows,columns=np.nonzero(rgba[:,:,3])
            self.assertLessEqual(columns.max()-columns.min()+1,2*point[2]*641+2)
            self.assertLessEqual(rows.max()-rows.min()+1,2*point[3]*361+2)
            sizes.append(np.count_nonzero(rgba[:,:,3]))
        self.assertLess(sizes[0],sizes[1]);self.assertLess(sizes[1],sizes[2]);np.testing.assert_array_equal(values,before)

    def test_camera_gradient_is_declared_as_display_and_does_not_modify_response(self):
        panel=self.panel();frame=video_frame();frame.image.fill(QColor('#00032e'))
        direction,response=observations();values=np.full((13,25),.7,np.float32);values.setflags(write=False)
        response=replace(response,values=values);before=values.copy();self.attach(panel,frame,pair=(direction,response))
        snapshot=panel.overlay_snapshot();self.assertTrue(snapshot['color_falloff_display_only'])
        self.assertFalse(snapshot['raw_response_modified']);self.assertIn('COLOR_FALLOFF',snapshot['overlay_render_mode'])
        np.testing.assert_array_equal(response.values,before)
        panel.resize(1250,850);panel.show();self.app.processEvents()
        panel.canvas.message='';panel.canvas.update();self.app.processEvents()
        out=ROOT/'build/verification/array_spherical_20261101';out.mkdir(parents=True,exist_ok=True)
        self.assertTrue(panel.canvas.grab().save(str(out/'offline_gradient_overlay.png')))

    def test_alignment_dialog_background_and_input_remain_readable(self):
        panel=self.panel();captured=[]
        def inspect():
            dialog=self.app.activeModalWidget();self.assertIsInstance(dialog,QDialog)
            image=dialog.grab().toImage()
            self.assertGreater(min(image.pixelColor(5,5).getRgb()[:3]),200)
            spin=dialog.findChildren(QDoubleSpinBox)[0]
            self.assertGreater(spin.palette().base().color().lightness(),200)
            self.assertLess(spin.palette().text().color().lightness(),100)
            out=ROOT/'build/verification/fast_demo_refinement_20261031'
            out.mkdir(parents=True,exist_ok=True)
            captured.append(image.save(str(out/'offline_alignment_dialog.png')))
            dialog.reject()
        QTimer.singleShot(0,inspect);panel._alignment_settings()
        self.assertEqual(captured,[True])

    def test_installed_angle_axes_and_screenshot_example(self):
        self.assertEqual(installed_angles(0.,10.,90.),(-10.,0.))
        az,el=installed_angles(10.,0.,90.)
        self.assertAlmostEqual(az,0.);self.assertAlmostEqual(el,10.)
        az,el=installed_angles(-10.,-5.,90.)
        self.assertAlmostEqual(az,5.076733016569718);self.assertAlmostEqual(el,-9.961558098090368)
        for raw in [(-60.,-30.),(0.,0.),(35.,20.)]:
            for roll in (-90.,37.,90.):
                back=installed_angles(*installed_angles(*raw,roll),-roll)
                np.testing.assert_allclose(back,raw,atol=1e-12)

    def test_array_angle_readout_and_plane_projection_keep_original_frame(self):
        window=LocalizationDemoWindow(trace_root=self.folder/'trace')
        self.addCleanup(window.close);window._timer.stop();window.video_panel._timer.stop()
        window.video_panel._settings_path=self.folder/'camera.json';window.video_panel._alignment_path=self.folder/'alignment.json'
        pair=observations(0,-10.,-5.);self.attach(window.video_panel,pair=pair)
        window.video_panel.alignment=PreviewAlignment(roll_deg=90.)
        self.assertTrue(window._apply_fast_direction(pair[0]))
        window._last_heatmap=pair[1];window._apply_fast_map()
        self.assertEqual(window.source_table.item(0,1).text(),'296.7°')
        self.assertEqual(window.source_table.item(0,2).text(),'78.8°')
        self.assertIs(window._fast_current,pair[0]);self.assertEqual(pair[0].azimuth_deg,-10.)
        image=window.direction.image.image;row,column=np.unravel_index(np.nanargmax(image),image.shape)
        self.assertAlmostEqual(-66.25+(column+.5)*132.5/image.shape[1],5.1,delta=2.5)
        self.assertAlmostEqual(-66.25+(row+.5)*132.5/image.shape[0],-10.,delta=2.5)
        self.assertTrue(np.isnan(image[0,0]))
        self.assertIs(window.video_panel._physical_response[1],pair[1])
        path=self.folder/'angles.csv';self.assertTrue(window.export_results(path))
        with path.open(encoding='utf-8-sig',newline='') as stream:exported=list(csv.DictReader(stream))
        self.assertEqual(exported[0]['coordinate_frame'],'ARRAY_SPHERICAL_MIC12_ZERO')
        self.assertEqual(float(exported[0]['raw_azimuth_deg']),-10.)
        self.assertAlmostEqual(float(exported[0]['azimuth_deg']),296.7402053557271)

    def test_direction_notification_is_bounded_and_applies_with_poll_timer_stopped(self):
        window=LocalizationDemoWindow(trace_root=self.folder/'trace')
        window._timer.stop();window.video_panel._timer.stop()
        worker=SourceNetworkWorker('127.0.0.1',5001,'127.0.0.1')
        class Running:
            def isRunning(self):return True
        window._source_udp_worker=worker;window._source_udp_thread=Running()
        window._mode='source_udp';window._active=True;window._fast_display_mode=True
        def cleanup():
            window._source_udp_worker=window._source_udp_thread=None
            window._active=False;window.close()
        self.addCleanup(cleanup)
        count=[];worker.fast_frame_ready.connect(lambda identity:count.append(identity))
        worker.fast_frame_ready.connect(window._fast_packet_arrived,Qt.ConnectionType.QueuedConnection)
        frames=[observations(i)[0] for i in range(3)]
        thread=threading.Thread(target=lambda:[worker._publish('fast_directions',frame) for frame in frames])
        thread.start();thread.join()
        self.assertIsNone(window._fast_current)
        self.app.processEvents()
        self.assertIs(window._fast_current,frames[-1]);self.assertEqual(len(count),1)
        self.assertFalse(window._timer.isActive())

    def test_heatmap_notification_pairs_current_data_without_poll_or_real_socket(self):
        window=LocalizationDemoWindow(trace_root=self.folder/'trace')
        self.addCleanup(window.close);window._timer.stop();window.video_panel._timer.stop()
        window._fast_display_mode=True
        pair=observations();self.attach(window.video_panel,pair=pair);window._apply_fast_direction(pair[0])
        receiver=HeatmapReceiver('127.0.0.1',0,'127.0.0.1',notify_ready=window.heatmap_frame_ready.emit)
        window._heatmap_receiver=receiver
        def receive_fixture():
            with receiver._lock:receiver._latest=pair[1]
            receiver.notify_ready()
        thread=threading.Thread(target=receive_fixture);thread.start();thread.join()
        self.assertIsNone(window._last_heatmap)
        self.app.processEvents()
        self.assertIs(window._last_heatmap,pair[1])
        self.assertIsNotNone(window.video_panel.overlay_snapshot())
        self.assertIsNone(receiver._socket)

    def multipoint_fixture(self):
        mapping=BearingImageMap((.72,.05,.5,.02,-1.,.5,.06,.03,1.))
        angles=[(-20.,-10.),(20.,-10.),(-20.,10.),(20.,10.),(0.,0.)]
        records=[dict(angles=list(a),uv=list(mapping.project(*a))) for a in angles]
        return mapping,records

    def test_four_point_fit_predicts_unseen_angles_and_inverse_rays(self):
        expected,records=self.multipoint_fixture();actual=fit_bearing_image_map(records[:4])
        for az,el in [(-15.,-5.),(5.,15.),(0.,0.),(25.,0.)]:
            uv=actual.project(az,el);np.testing.assert_allclose(uv,expected.project(az,el),atol=1e-12)
            x,y,z=actual.inverse_rays(*uv)
            self.assertAlmostEqual(float(np.rad2deg(np.arctan2(x,z))),az)
            self.assertAlmostEqual(float(np.rad2deg(np.arctan2(y,np.hypot(x,z)))),el)
        self.assertTrue(check_holdout(actual,records[-1],1280,720)['passed'])

    def test_degenerate_or_failed_independent_points_are_rejected(self):
        mapping,records=self.multipoint_fixture()
        with self.assertRaises(ValueError):fit_bearing_image_map([records[0]]*4)
        line=[dict(angles=[az,0.],uv=[.2+i*.15,.5]) for i,az in enumerate([-20.,-10.,0.,10.])]
        with self.assertRaises(ValueError):fit_bearing_image_map(line)
        wrong=dict(angles=[0.,0.],uv=[.8,.8]);self.assertFalse(check_holdout(mapping,wrong,1280,720)['passed'])
        with self.assertRaises(ValueError):check_holdout(mapping,dict(angles=[0.,0.],uv=[float('nan'),.5]),1280,720)
        binding=video_binding(video_frame().meta);validation=check_holdout(mapping,records[-1],1280,720)
        path=self.folder/'mapping.json'
        with self.assertRaises(ValueError):save_bearing_alignment(path,mapping,binding,90.,records[:4]+[records[0]],validation)
        self.assertFalse(path.exists())

    def test_multipoint_persistence_binds_camera_and_roll_and_rechecks_evidence(self):
        mapping,records=self.multipoint_fixture();path=self.folder/'mapping.json';binding=video_binding(video_frame().meta)
        validation=check_holdout(mapping,records[-1],1280,720)
        save_bearing_alignment(path,mapping,binding,90.,records,validation)
        restored,check=load_bearing_alignment(path,binding,90.)
        np.testing.assert_allclose(restored.array(),mapping.array());self.assertTrue(check['passed'])
        for roll,geometry in [(0.,binding),(90.,video_binding(video_frame(device='other').meta))]:
            with self.assertRaises(ValueError):load_bearing_alignment(path,geometry,roll)
        doc=json.loads(path.read_text(encoding='utf-8'))
        self.assertTrue(all(doc[k] is False for k in ['calibrated','intrinsics_measured','distance_estimated','physical_AV_sync_verified']))
        doc['records'][-1]['uv']=[.8,.8];path.write_text(json.dumps(doc),encoding='utf-8')
        with self.assertRaises(ValueError):load_bearing_alignment(path,binding,90.)
        self.assertEqual(list(self.folder.glob('*.tmp')),[])

    def test_multipoint_renderer_preserves_actual_grid_and_ignores_old_projection_offsets(self):
        mapping,_=self.multipoint_fixture();pair=observations(0,10.,5.);before=pair[1].values.copy()
        original=PreviewAlignment()
        other=PreviewAlignment(roll_deg=90.,mirror_x=True,offset_u=.15,offset_v=-.2)
        a=response_rgba(pair[1].values,original,640,360,bearing_map=mapping,focus_direction=(10.,5.))
        b=response_rgba(pair[1].values,other,640,360,bearing_map=mapping,focus_direction=(10.,5.))
        np.testing.assert_array_equal(a,b);np.testing.assert_array_equal(pair[1].values,before)
        row,col=np.unravel_index(a[:,:,3].argmax(),a.shape[:2]);u,v=mapping.project(10.,5.)
        self.assertAlmostEqual((col+.5)/640,u,delta=.02);self.assertAlmostEqual((row+.5)/360,v,delta=.02)
        self.assertGreater(a[row,col,3],0)
        outside=BearingImageMap((1.,0.,1.5,0.,1.,.5,0.,0.,1.))
        hidden=response_rgba(pair[1].values,original,640,360,bearing_map=outside,focus_direction=(10.,5.))
        self.assertFalse(hidden[:,:,3].any())

    def feed_stationary_point(self,panel,az,el,first_sequence):
        now=time.monotonic();frame=video_frame(sequence=first_sequence);panel.last_frame=frame
        for n in range(3):
            pair=tuple(replace(p,received_monotonic_s=now-(2-n)*.47) for p in observations(first_sequence+n,az,el))
            panel.set_spatial_response(*pair)

    def test_multipoint_ui_saves_only_after_independent_point_and_reopens(self):
        mapping,records=self.multipoint_fixture();panel=self.panel();self.attach(panel,pair=observations())
        panel.alignment=PreviewAlignment(roll_deg=90.);panel._persist_alignment();panel._begin_multi_alignment()
        self.assertTrue(panel.canvas.point_selection)
        for i,record in enumerate(records):
            self.feed_stationary_point(panel,*record['angles'],i*3+1)
            panel._align_to_video_point(*record['uv'])
            if i<4:
                self.assertFalse(panel._bearing_path.exists());self.assertIsNone(panel._active_bearing_map())
                self.assertIn(f'{i+2}/5',panel.detail.text())
        self.assertTrue(panel._bearing_path.exists());self.assertFalse(panel.canvas.point_selection)
        self.assertIsNotNone(panel._active_bearing_map())
        np.testing.assert_allclose(panel._active_bearing_map().array(),mapping.array(),atol=1e-12)
        self.assertTrue(panel.overlay_snapshot()['multipoint_alignment'])
        reopened=self.panel();self.attach(reopened,pair=observations())
        self.assertIsNotNone(reopened._active_bearing_map());reopened._begin_point_alignment()
        self.assertFalse(reopened.canvas.point_selection);self.assertIn('先在对齐设置中清除',reopened.detail.text())
        self.attach(reopened,video_frame(device='other'));self.assertIsNone(reopened._active_bearing_map())

    def test_multipoint_ui_keeps_old_projection_on_bad_fifth_point_and_rejects_jitter(self):
        _,records=self.multipoint_fixture();panel=self.panel();self.attach(panel,pair=observations())
        panel._begin_multi_alignment();original=panel.alignment
        for i,record in enumerate(records[:4]):
            self.feed_stationary_point(panel,*record['angles'],i*3+1);panel._align_to_video_point(*record['uv'])
        self.feed_stationary_point(panel,0.,0.,13);panel._align_to_video_point(.8,.8)
        self.assertFalse(panel._bearing_path.exists());self.assertIsNone(panel._active_bearing_map())
        self.assertEqual(len(panel._multi_points),4);self.assertEqual(panel.alignment,original)
        self.assertIn('独立核验偏差',panel.detail.text())
        panel._begin_point_alignment();panel._begin_multi_alignment()
        for i,az in enumerate([-20.,0.,20.]):panel.set_spatial_response(*observations(20+i,az,0.))
        panel._align_to_video_point(.5,.5);self.assertEqual(panel._multi_points,[])
        self.assertIn('方向仍在跳动',panel.detail.text())

    def test_multipoint_click_uses_unflipped_video_and_instructions_survive_video_poll(self):
        mapping,records=self.multipoint_fixture();panel=self.panel();self.attach(panel,pair=observations())
        panel.resize(1400,850);panel.show();self.app.processEvents();panel.display_flip.setChecked(True)
        panel._begin_multi_alignment();record=records[0];self.feed_stationary_point(panel,*record['angles'],1)
        rect=panel.canvas.image_rect();u,v=record['uv']
        point=QPoint(round(rect.left()+(1.-u)*rect.width()),round(rect.top()+v*rect.height()))
        QTest.mouseClick(panel.canvas,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,point)
        np.testing.assert_allclose(panel._multi_points[0]['uv'],[u,v],atol=1./rect.height())
        class FakeCamera:
            def take_latest(self):return video_frame(sequence=99)
            def snapshot(self):return dict(fps=30.,replaced=0,error=None,running=True)
        panel.usb_receiver=FakeCamera()
        try:
            panel._poll();self.assertIn('2/5',panel.detail.text());self.assertIn('右上',panel.canvas.message)
        finally:panel.usb_receiver=None

    def test_valid_direction_heatmap_handoff_is_atomic_and_bounded(self):
        panel=self.panel();old=observations();self.attach(panel,pair=old)
        self.assertIsNotNone(panel.overlay_snapshot());old_key=panel._physical_overlay_key
        new=observations(1,5.,0.);panel.expect_spatial_response(new[0]);panel._update_overlay()
        self.assertFalse(panel.canvas.spatial_overlay.isNull());self.assertEqual(panel._physical_overlay_key,old_key)
        self.assertIs(panel._physical_response[0],old[0]);self.assertIsNone(panel.overlay_snapshot())
        panel.set_spatial_response(*old);self.assertEqual(panel._pending_direction.key,new[0].key)
        panel.set_spatial_response(*new);self.assertEqual(panel.overlay_snapshot()['key'],list(new[0].key))
        next_pair=observations(2,10.,0.);panel.expect_spatial_response(next_pair[0]);panel._pending_since=time.monotonic()-.081
        panel._update_overlay();self.assertTrue(panel.canvas.spatial_overlay.isNull());self.assertIsNone(panel.overlay_snapshot())
        panel.set_spatial_response(*next_pair);self.assertFalse(panel.canvas.spatial_overlay.isNull())
        epoch=replace(observations(3)[0],epoch=2);panel.expect_spatial_response(epoch)
        self.assertTrue(panel.canvas.spatial_overlay.isNull());self.assertEqual(len(panel._recent_directions),0)
        panel.clear_spatial_response();self.assertIsNone(panel._pending_direction)

    def test_main_direction_handoff_does_not_flash_but_quality_rejection_clears(self):
        window=LocalizationDemoWindow(trace_root=self.folder/'trace');self.addCleanup(window.close)
        window._timer.stop();window.video_panel._timer.stop();window._fast_display_mode=True
        old=observations();self.attach(window.video_panel,pair=old);window._apply_fast_direction(old[0])
        window._last_heatmap=old[1];window._apply_fast_map()
        self.assertFalse(window.video_panel.canvas.spatial_overlay.isNull())
        new=observations(1,5.,0.);window._apply_fast_direction(new[0])
        self.assertFalse(window.video_panel.canvas.spatial_overlay.isNull());self.assertIsNone(window.video_panel.overlay_snapshot())
        window._last_heatmap=new[1];window._apply_fast_map()
        self.assertEqual(window.video_panel.overlay_snapshot()['key'],list(new[0].key))
        rejected=observations(2,valid=False);window._apply_fast_direction(rejected[0])
        self.assertTrue(window.video_panel.canvas.spatial_overlay.isNull());self.assertIsNone(window.video_panel.overlay_snapshot())


if __name__=='__main__':
    app=LocalizationDemoApplication([])
    family=_install_cjk_font();app.setFont(QFont(family,10));app.setStyleSheet(compact_stylesheet(family))
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(FastDemoAlignmentChecks)
    # 原USB生命周期检查复用，但配置重定向到临时目录，不改用户已经选好的真实相机。
    sys.path.insert(0,str(ROOT/'tests'))
    import test_usb_camera_preview as usb_checks
    with tempfile.TemporaryDirectory(prefix='mic16_usb_regression_') as temporary:
        assert Path(temporary).resolve().parent==Path(tempfile.gettempdir()).resolve()
        assert Path(temporary).name.startswith('mic16_usb_regression_')
        original_panel=usb_checks.VideoPanel
        usb_checks.VideoPanel=lambda *a,**kw:original_panel(*a,settings_path=Path(temporary)/'usb.json',**kw)
        suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(usb_checks.UsbChecks))
        result=unittest.TextTestRunner(verbosity=2).run(suite)
        usb_checks.VideoPanel=original_panel
    output=ROOT/'build/verification/fast_demo_refinement_20261031';output.mkdir(parents=True,exist_ok=True)
    (output/'result.json').write_text(json.dumps(dict(passed=result.wasSuccessful(),test_methods=result.testsRun,
        failures=len(result.failures),errors=len(result.errors),real_Qt=True,physical_camera_opened=False,
        board_operations=0,new_capture=False,acoustic_math_changed=False,
        physical_static_dynamic_AV_verified=False),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
