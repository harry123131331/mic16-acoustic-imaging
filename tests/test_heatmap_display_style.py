"""响应热图的颜色饱和与轴比例检查；不采集、不连接板卡或摄像头。"""
from pathlib import Path
from dataclasses import replace
import json,os,sys,tempfile,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host/python_tools'));sys.path.insert(0,str(ROOT/'tests'))
import numpy as np
from PySide6.QtGui import QFont
from mic16_qt.app import _install_cjk_font
from mic16_qt.presentation.compact_theme import compact_stylesheet
from mic16_qt.presentation.localization_demo import LocalizationDemoApplication,LocalizationDemoWindow
from mic16_av_fusion.heatmap_rendering import response_display_levels
from test_hemisphere_display import fixture

OUT=ROOT/'build/verification/heatmap_display_style_20261101'

class HeatmapStyleChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=LocalizationDemoApplication.instance() or LocalizationDemoApplication([])
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mic16_heatmap_style_');self.windows=[]
    def tearDown(self):
        for w in self.windows:w.close();w.deleteLater()
        self.app.processEvents();self.temp.cleanup()
    def window(self):
        w=LocalizationDemoWindow(trace_root=Path(self.temp.name)/'trace')
        w.direction.set_point_display_only(False)  # 仅检查保留的完整响应渲染器。
        w._timer.stop();w.video_panel._timer.stop();self.windows.append(w)
        w.resize(1450,950);w.show();self.app.processEvents();return w
    def test_automatic_upper_limit_removes_clipping_without_changing_values(self):
        values=np.array([[.1,.2,.4],[.3,.6,.8]])
        before=values.tobytes()
        self.assertEqual(response_display_levels(values,automatic=True),(.1,.8))
        self.assertEqual(response_display_levels(values,automatic=False),(.1,.35))
        self.assertEqual(response_display_levels(np.full((2,2),.2),automatic=True),(.1,.35))
        self.assertEqual(response_display_levels(np.full((2,2),np.nan),automatic=True),(.1,.35))
        self.assertEqual(values.tobytes(),before)
        with self.assertRaises(ValueError):response_display_levels(values,(1.,1.))
    def test_one_degree_horizontal_and_vertical_have_same_screen_length(self):
        w=self.window();d,h=fixture();w._apply_fast_direction(d);w._last_heatmap=h;w._apply_fast_map()
        w.direction.resize(1080,600);w.direction._fit_plot();self.app.processEvents();w.direction._fit_plot()
        dx,dy=w.direction.plot.getViewBox().viewPixelSize()
        self.assertAlmostEqual(abs(dx/dy),1.,places=7)
        self.assertEqual(w.direction._angle_limits,(0.,360.,0.,90.))
        np.testing.assert_allclose(w.direction.plot.getViewBox().viewRange(),[[0.,360.],[0.,90.]],atol=1.)
    def test_default_auto_and_manual_selection_keep_camera_threshold_separate(self):
        w=self.window();d,h=fixture();w._apply_fast_direction(d);w._last_heatmap=h;w._apply_fast_map()
        self.assertEqual(w.heatmap_scale.currentText(),'色标 自动')
        self.assertTrue(w.direction.response_auto_scale)
        self.assertEqual(w.direction.display_levels,(.1,float(h.values.max())))
        self.assertEqual(w.video_panel._response_levels,(.1,.35))
        w.heatmap_scale.setCurrentIndex(1)
        self.assertFalse(w.direction.response_auto_scale);self.assertEqual(w.direction.display_levels,(.1,.35))
        self.assertIs(w._fast_current,d);self.assertEqual(w.source_table.item(0,1).text(),'60.0°')
    def test_offline_render_has_true_colorbar_and_unchanged_source(self):
        # 只用于显示的数值夹具：检验旧0.35上限会饱和、自动色条是否保留0.8峰值。
        w=self.window();d,h=fixture(155)
        phi,epsilon=np.meshgrid(np.arange(25)*15.,np.arange(13)*7.5)
        delta=(phi-75.+180.)%360.-180.
        values=(.1+.7*np.exp(-(delta/26.)**2-((epsilon-45.)/24.)**2)).astype(np.float32)
        original=values.tobytes();h=replace(h,values=values)
        w._apply_fast_direction(d);w._last_heatmap=h;w._apply_fast_map()
        self.assertAlmostEqual(w.direction.display_levels[1],.8,places=6)
        self.assertEqual(values.tobytes(),original);self.assertIs(w._fast_current,d)
        self.assertEqual(w.direction.plot.getViewBox().state['background'],'#00032e')
        w.direction.resize(1080,540);w.direction._fit_plot();self.app.processEvents();w.direction._fit_plot()
        OUT.mkdir(parents=True,exist_ok=True)
        self.assertTrue(w.direction.plot.grab().save(str(OUT/'offline_color_and_aspect_preview.png')))

if __name__=='__main__':
    app=LocalizationDemoApplication([]);family=_install_cjk_font();app.setFont(QFont(family,10));app.setStyleSheet(compact_stylesheet(family))
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(HeatmapStyleChecks))
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'result.json').write_text(json.dumps(dict(passed=result.wasSuccessful(),test_methods=result.testsRun,
        failures=len(result.failures),errors=len(result.errors),board_operations=0,new_capture=False,
        fixture='DISPLAY_ONLY_NOT_ACOUSTIC_SIMULATION',physical_accuracy_verified=False),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
