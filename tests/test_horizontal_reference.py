"""水平基准、相机隔离与冻结C几何的独立离线核验；不产生或采集音频。"""
from pathlib import Path
from dataclasses import replace
import ctypes
import csv
import hashlib
import json
import math
import os
import sys
import tempfile
import unittest

os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host/python_tools'));sys.path.insert(0,str(ROOT/'tests'))
import numpy as np
from PySide6.QtWidgets import QApplication
from mic16_av_fusion.array_installation import (
    VerticalArrayInstallation,vertical_installation_document,load_vertical_installation,GEOMETRY_SHA256,REFERENCE_PLANE,
)
from mic16_av_fusion.preview_projection import PreviewAlignment
from mic16_av_fusion.installation_coordinates import installed_angles,installed_response_grid
from mic16_qt.presentation.localization_demo import LocalizationDemoApplication,LocalizationDemoWindow
from test_fast_demo_alignment import observations,video_frame

OUT=ROOT/'build/verification/horizontal_reference_20261101'


class HorizontalReferenceChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=QApplication.instance() or LocalizationDemoApplication([])

    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='mic16_horizontal_')
        self.folder=Path(self.temporary.name);self.windows=[]
        self.path=self.folder/'array_installation.json'
        self.path.write_text(json.dumps(vertical_installation_document(user_confirmed=True)),encoding='utf-8')

    def tearDown(self):
        for window in self.windows:window.close();window.deleteLater()
        self.app.processEvents();self.temporary.cleanup()

    def window(self):
        window=LocalizationDemoWindow(installation_path=self.path,trace_root=self.folder/'trace')
        window._timer.stop();window.video_panel._timer.stop();self.windows.append(window)
        return window

    def test_profile_records_horizontal_zero_and_rejects_inconsistent_numbering(self):
        profile=load_vertical_installation(self.path)
        self.assertEqual(profile.reference_plane,REFERENCE_PLANE)
        self.assertTrue(profile.user_confirmed);self.assertFalse(profile.pose_measured)
        self.assertEqual(profile.roll_deg,90.)
        self.assertEqual(profile.cardinal_microphones,dict(up=0,left=4,down=8,right=12))
        original=json.loads(self.path.read_text())
        for key,value in [('geometry_sha256','invalid'),('reference_plane','ARRAY_PLANE'),('pose_measured',True),('user_confirmed','yes')]:
            bad=dict(original);bad[key]=value;self.path.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):load_vertical_installation(self.path)
        bad=dict(original);bad['cardinal_microphones']=dict(up=0,left=12,down=8,right=4)
        self.path.write_text(json.dumps(bad))
        with self.assertRaises(ValueError):load_vertical_installation(self.path)

    def test_all_quarter_mounts_map_the_actual_upper_and_right_axes(self):
        for upper in (0,4,8,12):
            self.path.write_text(json.dumps(vertical_installation_document(up_microphone=upper,user_confirmed=True)))
            profile=load_vertical_installation(self.path);theta=np.deg2rad(upper*22.5)
            top=np.array([np.cos(theta),np.sin(theta),0.]);right=np.array([top[1],-top[0],0.])
            for vector,expected in [(top,(0.,20.)),(right,(20.,0.))]:
                ray=np.sin(np.deg2rad(20.))*vector+np.array([0.,0.,np.cos(np.deg2rad(20.))])
                raw=(np.rad2deg(np.arctan2(ray[0],ray[2])),np.rad2deg(np.arctan2(ray[1],np.hypot(ray[0],ray[2]))))
                np.testing.assert_allclose(installed_angles(*raw,profile.roll_deg),expected,atol=1e-12)

    def test_zero_positive_negative_height_from_independent_cartesian_positions(self):
        for x,height,z in [(0.,0.,1.),(0.,.2,1.),(0.,-.2,1.),(.3,.2,1.),(-.3,-.2,1.)]:
            raw_vector=np.array([height,-x,z]);raw_vector/=np.linalg.norm(raw_vector)
            raw=(np.rad2deg(np.arctan2(raw_vector[0],raw_vector[2])),np.rad2deg(np.arcsin(raw_vector[1])))
            result=installed_angles(*raw,90.)
            expected=(np.rad2deg(np.arctan2(x,z)),np.rad2deg(np.arctan2(height,np.hypot(x,z))))
            np.testing.assert_allclose(result,expected,atol=1e-12)
            if height: self.assertEqual(math.copysign(1.,result[1]),math.copysign(1.,height))

    def test_camera_device_mode_and_projection_cannot_change_acoustic_angles(self):
        window=self.window();panel=window.video_panel
        for sequence,(camera,roll) in enumerate([('camera-a',0.),('camera-b',-90.),('camera-c',180.),('camera-d',90.)]):
            frame=video_frame(device=camera,width=640 if sequence%2 else 1280,height=480 if sequence%2 else 720)
            panel.last_frame=frame;panel._alignment_binding={'mount_id':camera}
            panel.alignment=PreviewAlignment(roll_deg=roll,yaw_deg=25.,pitch_deg=-15.,mirror_x=bool(sequence%2))
            pair=observations(sequence,10.,0.);self.assertTrue(window._apply_fast_direction(pair[0]))
            self.assertEqual(window.source_table.item(0,1).text(),'+0.0°')
            self.assertEqual(window.source_table.item(0,2).text(),'+10.0°')
            self.assertEqual(window.direction.installation_roll_deg,90.)
            self.assertEqual(window._history[-1]['angle_reference_plane'],REFERENCE_PLANE)
        self.assertTrue(all(e['roll_deg']==90. for e in window.demo_trace.events if e['kind']=='installed_direction_presented'))

    def test_missing_or_invalid_installation_does_not_fall_back_to_camera_or_claim_confirmed(self):
        self.path.unlink();window=self.window();panel=window.video_panel
        panel.last_frame=video_frame();panel._alignment_binding={'mount_id':'fake'}
        panel.alignment=PreviewAlignment(roll_deg=-90.)
        window._apply_fast_direction(observations(0,10.,0.)[0])
        self.assertFalse(window._array_installation.user_confirmed)
        self.assertIn('阵列安装待确认',window.result_state.text())
        record=next(e for e in window.demo_trace.events if e['kind']=='array_installation_loaded')
        self.assertFalse(record['user_confirmed']);self.assertIsNotNone(record['error'])

    def test_table_map_log_and_csv_keep_reference_and_raw_values_separate(self):
        window=self.window();pair=observations(0,10.,0.)
        window._apply_fast_direction(pair[0]);window._last_heatmap=pair[1];window._apply_fast_map()
        self.assertIs(window._fast_current,pair[0]);self.assertEqual(pair[0].elevation_deg,0.)
        image=window.direction.image.image;row,col=np.unravel_index(np.nanargmax(image),image.shape)
        self.assertAlmostEqual(-66.25+(col+.5)*132.5/image.shape[1],0.,delta=2.5);self.assertAlmostEqual(-66.25+(row+.5)*132.5/image.shape[0],10.,delta=2.5)
        exported=self.folder/'angles.csv';window.export_results(exported)
        with exported.open(encoding='utf-8-sig',newline='') as stream:record=next(csv.DictReader(stream))
        self.assertEqual(record['angle_reference_plane'],REFERENCE_PLANE)
        self.assertEqual(record['installation_user_confirmed'],'True');self.assertEqual(record['installation_pose_measured'],'False')
        self.assertEqual(float(record['raw_azimuth_deg']),10.);self.assertEqual(float(record['raw_elevation_deg']),0.)
        self.assertAlmostEqual(float(record['elevation_deg']),10.)

    def test_independent_source_geometry_agrees_with_original_frozen_C_arrivals(self):
        # 这里直接从用户描述的实体编号布置构造坐标，不调用生产方向投影函数生成期望值。
        dll_path=ROOT/'sim/geometry_reference.dll'
        if not dll_path.is_file():self.skipTest('原始独立C几何夹具DLL未随提交副本分发')
        self.assertEqual(hashlib.sha256(dll_path.read_bytes()).hexdigest(),'1d5a163b697b9e85a12dc6354fca9fd1509b38f84092e4b89ccc7f2a48b617f7')
        self.assertEqual(hashlib.sha256((ROOT/'firmware/modules/physical/include/mic16_mic_positions.h').read_bytes()).hexdigest(),GEOMETRY_SHA256)
        library=ctypes.CDLL(str(dll_path));function=library.mic16_candidate_geometry
        function.argtypes=[ctypes.POINTER(ctypes.c_double),ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_float)]
        function.restype=ctypes.c_int
        angle=np.arange(16)*2*np.pi/16
        microphones=np.column_stack([-.08*np.sin(angle),.08*np.cos(angle),np.zeros(16)])
        pairs=[(p,(p+1)%16) if p<16 else ((p-16,p-8) if p<24 else (p-24,p-20)) for p in range(32)]
        maximum_arrival=maximum_lag=0.;cases=0
        for x in [-.4,-.2,0.,.2,.4]:
            for height in [-.4,-.2,0.,.2,.4]:
                ray=np.array([x,height,1.]);ray/=np.linalg.norm(ray)
                raw=np.array([np.rad2deg(np.arctan2(ray[1],ray[2])),np.rad2deg(np.arcsin(-ray[0]))],dtype=np.float64)
                arrivals=np.zeros(16,np.float32);lags=np.zeros(32,np.float32)
                self.assertEqual(function(raw.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    arrivals.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),lags.ctypes.data_as(ctypes.POINTER(ctypes.c_float))),0)
                expected=-microphones@ray/343.*39062.5;expected-=expected.mean()
                wanted_lags=np.array([expected[b]-expected[a] for a,b in pairs])
                maximum_arrival=max(maximum_arrival,float(np.max(np.abs(expected-arrivals))))
                maximum_lag=max(maximum_lag,float(np.max(np.abs(wanted_lags-lags))))
                np.testing.assert_allclose(arrivals,expected,atol=1e-5,rtol=0)
                np.testing.assert_allclose(lags,wanted_lags,atol=1e-5,rtol=0)
                if x==0. and height>0.:self.assertLess(arrivals[0],arrivals[8])
                if x==0. and height<0.:self.assertGreater(arrivals[0],arrivals[8])
                if height==0. and x>0.:self.assertLess(arrivals[12],arrivals[4])
                if height==0. and x<0.:self.assertGreater(arrivals[12],arrivals[4])
                cases+=1
        OUT.mkdir(parents=True,exist_ok=True)
        (OUT/'geometry_result.json').write_text(json.dumps(dict(passed=True,cases=cases,
            original_C_geometry_sha256=hashlib.sha256(dll_path.read_bytes()).hexdigest(),
            microphone_geometry_sha256=GEOMETRY_SHA256,maximum_arrival_error_samples=maximum_arrival,
            maximum_pair_lag_error_samples=maximum_lag,board_operations=0,physical_wiring_verified=False,
            role='ANALYTICAL_GEOMETRY_CHECK_NOT_ACOUSTIC_ACCURACY'),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(HorizontalReferenceChecks))
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'result.json').write_text(json.dumps(dict(passed=result.wasSuccessful(),test_methods=result.testsRun,
        failures=len(result.failures),errors=len(result.errors),new_capture=False,board_operations=0,
        physical_camera_opened=False,physical_horizontal_angles_verified=False),indent=2)+'\n',encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
