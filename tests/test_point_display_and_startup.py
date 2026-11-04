"""小圆点、相机独立性和人工启动入口检查；不读取摄像头或访问板卡。"""
from pathlib import Path
from dataclasses import replace
import importlib.util,json,os,sys,tempfile,unittest
from unittest.mock import patch
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host/python_tools'),str(ROOT/'tests'),str(ROOT/'scripts')]
import numpy as np
from PySide6.QtGui import QFont
from mic16_qt.app import _install_cjk_font
from mic16_qt.presentation.compact_theme import compact_stylesheet
from mic16_qt.presentation.localization_demo import LocalizationDemoApplication,LocalizationDemoWindow
from test_hemisphere_display import fixture
from test_fast_demo_alignment import video_frame
from mic16_av_fusion.preview_projection import PreviewAlignment
import start_localization as startup

OUT=ROOT/'build/verification/point_display_startup_cleanup_20261101'

class PointAndStartupChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=LocalizationDemoApplication.instance() or LocalizationDemoApplication([])
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mic16_point_display_');self.windows=[]
    def tearDown(self):
        for w in self.windows:w.close();w.deleteLater()
        self.app.observed_window=None;self.app.processEvents();self.temp.cleanup()
    def window(self):
        w=LocalizationDemoWindow(trace_root=Path(self.temp.name)/'trace')
        w._timer.stop();w.video_panel._timer.stop();self.windows.append(w)
        w.resize(1450,900);w.show();self.app.processEvents();return w
    def feed(self,w,d,h):
        self.assertTrue(w._apply_fast_direction(d));w._last_heatmap=h;w._apply_fast_map()
    def test_default_only_shows_small_circle_on_deep_blue(self):
        w=self.window();d,h=fixture(155);self.feed(w,d,h)
        v=w.direction
        self.assertTrue(v.point_display_only);self.assertFalse(v.image.isVisible());self.assertFalse(v.colorbar.isVisible())
        self.assertTrue(all(not c.isVisible() for c in v.contours))
        self.assertEqual(v.plot.getViewBox().state['background'],'#00032e')
        x,y=v.markers.getData();np.testing.assert_allclose([x[0],y[0]],[75.,45.],atol=1e-12)
        self.assertEqual(v.markers.opts['size'],9);self.assertEqual(len(v.labels),1)
        self.assertTrue(w.heatmap_scale.isHidden());self.assertTrue(w.heatmap_button.isHidden())
        self.assertEqual(w.source_table.item(0,1).text(),'75.0°');self.assertEqual(w.source_table.item(0,2).text(),'45.0°')
    def test_main_point_does_not_clear_camera_response_or_change_data(self):
        w=self.window();d,h=fixture(155);before=h.values.tobytes()
        p=w.video_panel;frame=video_frame();p.last_frame=frame;p.canvas.image=frame.image
        p._restore_alignment_for_frame(frame)
        p.alignment=PreviewAlignment(roll_deg=90.,horizontal_fov_deg=120.,vertical_fov_deg=100.)
        p.overlay_enabled.setChecked(True);self.feed(w,d,h)
        self.assertIs(w._fast_current,d);self.assertIs(w._last_heatmap,h)
        self.assertEqual(h.values.tobytes(),before);self.assertEqual(p._response_levels,(.1,.35))
        self.assertIsNotNone(p.canvas.spatial_overlay)
        self.assertFalse(w.direction.image.isVisible());self.assertGreater(p.canvas.spatial_overlay.width(),0)
    def test_invalid_result_and_clear_remove_old_circle(self):
        w=self.window();d,h=fixture(155);self.feed(w,d,h)
        w.direction.show_result((),('invalid',));x,y=w.direction.markers.getData();self.assertEqual(len(x),0)
        self.assertFalse(w.direction.image.isVisible());self.assertEqual(len(w.direction.labels),0)
        w.direction.clear('已停止');self.assertIsNone(w.direction._sources_key)
        self.assertFalse(w.direction.colorbar.isVisible());self.assertEqual(len(w.direction.markers.getData()[0]),0)
    def test_new_directions_move_circle_duplicate_is_not_new_result(self):
        w=self.window();d,h=fixture(155);self.feed(w,d,h)
        self.assertFalse(w._apply_fast_direction(d));self.assertEqual(w.source_table.item(0,1).text(),'75.0°')
        d2,h2=fixture(160)
        changes=dict(sequence=1,first_sample=8192,end_sample=16384,completed_tick=d.completed_tick+5000,
                     first_received_tick=d.first_received_tick+5000,last_front_tick=d.last_front_tick+5000)
        d2=replace(d2,**changes);h2=replace(h2,**changes);self.feed(w,d2,h2)
        x,y=w.direction.markers.getData();np.testing.assert_allclose([x[0],y[0]],[150.,45.],atol=1e-12)
        self.assertFalse(w.direction.image.isVisible())
    def test_circle_size_stays_fixed_and_offline_preview(self):
        w=self.window();d,h=fixture(155);self.feed(w,d,h)
        for width in [700,1080]:
            w.direction.resize(width,500);w.direction._fit_plot();self.app.processEvents()
            self.assertEqual(w.direction.markers.opts['size'],9)
        w.direction._fit_plot();self.app.processEvents();OUT.mkdir(parents=True,exist_ok=True)
        self.assertTrue(w.direction.plot.grab().save(str(OUT/'offline_point_display_preview.png')))
    def test_manual_check_does_not_load_or_open_gui(self):
        with patch.object(startup.common,'check') as check,patch.object(startup.common,'main') as load,patch.object(startup,'open_software') as gui:
            with patch.object(sys,'argv',['start_localization.py','--check']):startup.main()
            check.assert_called_once();load.assert_not_called();gui.assert_not_called()
            self.assertEqual(startup.common.OUT.name,startup.PACKAGE)
    def test_manual_load_opens_gui_only_after_success(self):
        order=[]
        with patch.object(startup.common,'main',side_effect=lambda:order.append('load')),patch.object(startup,'open_software',side_effect=lambda:order.append('gui')):
            with patch.object(sys,'argv',['start_localization.py','--load']):startup.main()
        self.assertEqual(order,['load','gui'])
        with patch.object(startup.common,'main',side_effect=RuntimeError('模拟加载失败')),patch.object(startup,'open_software') as gui:
            with patch.object(sys,'argv',['start_localization.py','--load']):
                with self.assertRaises(RuntimeError):startup.main()
            gui.assert_not_called()
    def test_vitis_and_cmd_select_same_current_package(self):
        text=(ROOT/'scripts/load_localization_vitis.tcl').read_text(encoding='utf-8')
        self.assertIn(startup.PACKAGE+'/hemisphere_localization.elf',text)
        for name,action in [('load_localization_demo.cmd','--load'),('check_localization_demo.cmd','--check')]:
            raw=(ROOT/name).read_bytes();self.assertIn(b'\r\n',raw)
            self.assertNotIn(b'\n',raw.replace(b'\r\n',b''))
            self.assertIn('start_localization.py',raw.decode('utf-8'));self.assertIn(action,raw.decode('utf-8'))

if __name__=='__main__':
    app=LocalizationDemoApplication([]);family=_install_cjk_font();app.setFont(QFont(family,10));app.setStyleSheet(compact_stylesheet(family))
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PointAndStartupChecks))
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'result.json').write_text(json.dumps(dict(passed=result.wasSuccessful(),test_methods=result.testsRun,
        failures=len(result.failures),errors=len(result.errors),board_operations=0,new_capture=False,
        preview='OFFLINE_DISPLAY_FIXTURE',physical_accuracy_verified=False),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
