"""用户明确的板面球坐标、法线奇点与真实响应方向图的离线检查。"""
from pathlib import Path
import csv
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
from PySide6.QtGui import QFont
from mic16_qt.app import _install_cjk_font
from mic16_qt.presentation.compact_theme import compact_stylesheet
from mic16_qt.presentation.localization_demo import LocalizationDemoApplication,LocalizationDemoWindow
from mic16_av_fusion.array_spherical_coordinates import (
    array_spherical_angles,array_direction_vector,array_projection,display_view_angles,
)
from mic16_av_fusion.installation_coordinates import installed_response_grid,installed_angles
from mic16_av_fusion.array_installation import load_vertical_installation,ARRAY_REFERENCE_PLANE
from mic16_av_fusion.preview_projection import PreviewAlignment
from test_fast_demo_alignment import observations,video_frame

OUT=ROOT/'build/verification/array_spherical_20261101'


class ArraySphericalChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=QApplication.instance() or LocalizationDemoApplication([])

    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='mic16_array_angles_')
        self.folder=Path(self.temporary.name);self.windows=[]

    def tearDown(self):
        for window in self.windows:window.close();window.deleteLater()
        self.app.processEvents();self.temporary.cleanup()

    def window(self):
        window=LocalizationDemoWindow(trace_root=self.folder/'trace')
        window.direction.set_point_display_only(False)  # 仅检查保留的完整响应渲染器。
        window._timer.stop();window.video_panel._timer.stop();self.windows.append(window)
        return window

    def test_user_cardinals_and_board_plane_zero(self):
        for raw,expected in [((0.,-30.),(0.,60.)),((10.,0.),(90.,80.)),
            ((0.,25.),(180.,65.)),((-20.,0.),(270.,70.)),((-20.,-5.),(284.34856765403896,69.40932832447321))]:
            np.testing.assert_allclose(array_spherical_angles(*raw),expected,atol=1e-12)
        for raw,expected in [((0.,-90.),0.),((90.,0.),90.),((0.,90.),180.),((-90.,0.),270.)]:
            az,el=array_spherical_angles(*raw);self.assertAlmostEqual(az,expected);self.assertAlmostEqual(el,0.)

    def test_normal_azimuth_is_undefined_and_full_original_grid_roundtrips(self):
        self.assertEqual(array_spherical_angles(0.,0.),(None,90.))
        count=0
        for az in np.arange(-60.,60.1,5.):
            for el in np.arange(-30.,30.1,5.):
                phi,epsilon=array_spherical_angles(float(az),float(el))
                x,y=array_projection(phi,epsilon);z=math.sin(math.radians(epsilon))
                # 原扫描向量先按MIC0上、MIC12右的实际轴关系独立变换。
                a,e=map(math.radians,(az,el));expected=(-math.sin(e),math.cos(e)*math.sin(a),math.cos(e)*math.cos(a))
                np.testing.assert_allclose([x,y,z],expected,atol=2e-15)
                np.testing.assert_allclose(display_view_angles(phi,epsilon),installed_angles(float(az),float(el),90.),atol=1e-12)
                self.assertGreater(epsilon,0.);count+=1
        self.assertEqual(count,325)
        with self.assertRaises(ValueError):array_projection(None,60.)

    def test_bad_angles_and_nan_spectrum_are_rejected(self):
        for args in [(float('nan'),0.),(0.,float('inf')),(True,0.),(181.,0.),(0.,91.)]:
            with self.assertRaises(ValueError):array_spherical_angles(*args)
        for values in [np.zeros((12,25)),np.full((13,25),np.nan)]:
            with self.assertRaises(ValueError):installed_response_grid(values,90.)

    def test_config_is_the_user_array_convention_and_bad_zero_mic_rejected(self):
        path=ROOT/'build/config/array_installation.json';profile=load_vertical_installation(path)
        self.assertEqual(profile.reference_plane,ARRAY_REFERENCE_PLANE);self.assertTrue(profile.user_confirmed)
        doc=json.loads(path.read_text(encoding='utf-8'));self.assertEqual(doc['azimuth_zero_microphone'],12)
        bad=self.folder/'bad.json';doc['azimuth_zero_microphone']=0;bad.write_text(json.dumps(doc),encoding='utf-8')
        with self.assertRaises(ValueError):load_vertical_installation(bad)

    def test_real_response_projection_preserves_mask_values_and_peak_position(self):
        pair=observations(0,10.,-5.);before=pair[1].values.copy()
        values,rect,limits=installed_response_grid(pair[1].values,90.)
        row,col=np.unravel_index(np.nanargmax(values),values.shape)
        np.testing.assert_allclose([-65.+col*2.5,-65.+row*2.5],[5.076733016569718,9.961558098090368],atol=2.5)
        self.assertTrue(np.isnan(values[0,0]));self.assertTrue(np.isnan(values[26,52]))
        self.assertTrue(np.isnan(values[52,26]));self.assertTrue(np.isfinite(values[26,26]))
        self.assertLessEqual(float(np.nanmax(values)),float(np.max(before)))
        np.testing.assert_array_equal(pair[1].values,before)
        self.assertEqual(limits,(-65.,65.,-65.,65.))

    def test_normal_table_csv_and_plot_do_not_fabricate_zero_azimuth(self):
        window=self.window();pair=observations();window._apply_fast_direction(pair[0])
        window._last_heatmap=pair[1];window._apply_fast_map()
        self.assertEqual(window.source_table.item(0,1).text(),'—')
        self.assertEqual(window.source_table.item(0,2).text(),'90.0°')
        x,y=window.direction.markers.getData();np.testing.assert_allclose([x[0],y[0]],[0.,0.],atol=1e-15)
        path=self.folder/'normal.csv';window.export_results(path)
        with path.open(encoding='utf-8-sig',newline='') as stream:row=next(csv.DictReader(stream))
        self.assertEqual(row['azimuth_deg'],'');self.assertEqual(row['azimuth_defined'],'False')
        self.assertEqual(float(row['elevation_deg']),90.);self.assertEqual(row['angle_reference_plane'],ARRAY_REFERENCE_PLANE)
        self.assertIs(window._fast_current,pair[0])

    def test_table_source_identity_projection_and_search_boundary_diagnostic(self):
        window=self.window();panel=window.video_panel;frame=video_frame()
        panel.last_frame=frame;panel.canvas.image=frame.image;panel._restore_alignment_for_frame(frame)
        panel.alignment=PreviewAlignment(roll_deg=-90.,mirror_x=True)
        pair=observations(0,0.,-30.);window._apply_fast_direction(pair[0]);window._last_heatmap=pair[1];window._apply_fast_map()
        self.assertEqual(window.source_table.item(0,1).text(),'0.0°');self.assertEqual(window.source_table.item(0,2).text(),'60.0°')
        self.assertIn('扫描边界',window.result_state.text())
        record=[e for e in window.demo_trace.events if e['kind']=='installed_direction_presented'][-1]
        self.assertEqual(record['physical_frame'],'ARRAY_SPHERICAL_MIC12_ZERO');self.assertTrue(record['search_boundary'])
        self.assertEqual(record['raw_elevation_deg'],-30.);self.assertTrue(record['azimuth_defined'])

    def test_direction_view_uses_same_projection_and_has_no_nan_crossing_contours(self):
        window=self.window();window.resize(1450,900);window.show();self.app.processEvents()
        pair=observations(0,10.,-5.);window._apply_fast_direction(pair[0]);window._last_heatmap=pair[1];window._apply_fast_map()
        self.app.processEvents();view=window.direction
        self.assertTrue(view.array_plane_mode);self.assertTrue(all(not curve.isVisible() for curve in view.contours))
        x,y=view.markers.getData();np.testing.assert_allclose([x[0],y[0]],[5.076733016569718,9.961558098090368],atol=1e-12)
        self.assertIn('水平偏角',view.plot.getAxis('bottom').labelText)
        self.assertIn('垂直偏角',view.plot.getAxis('left').labelText)
        self.assertTrue(view.image.isVisible());self.assertIsNone(window.video_panel.video_backend)
        OUT.mkdir(parents=True,exist_ok=True)
        self.assertTrue(view.plot.grab().save(str(OUT/'offline_rectangular_heatmap.png')))


if __name__=='__main__':
    app=LocalizationDemoApplication([]);family=_install_cjk_font();app.setFont(QFont(family,10));app.setStyleSheet(compact_stylesheet(family))
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ArraySphericalChecks))
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'result.json').write_text(json.dumps(dict(passed=result.wasSuccessful(),test_methods=result.testsRun,
        failures=len(result.failures),errors=len(result.errors),reference_plane='MICROPHONE_ARRAY_PLANE',
        azimuth_reference='MIC12=0,MIC0=90,MIC4=180,MIC8=270',raw_grid_directions_checked=325,
        acoustic_math_changed=False,physical_accuracy_verified=False,new_capture=False,board_operations=0),indent=2)+'\n',encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
