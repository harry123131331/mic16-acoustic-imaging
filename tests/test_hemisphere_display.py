"""前半球候选的真实C报文、Qt矩形网格和相机反投影离线检查。"""
from pathlib import Path
from dataclasses import replace
import json,math,os,struct,sys,tempfile,time,unittest,zlib
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host/python_tools'));sys.path.insert(0,str(ROOT/'tests'))
import numpy as np
from PySide6.QtGui import QFont,QImage
from mic16_qt.app import _install_cjk_font
from mic16_qt.presentation.compact_theme import compact_stylesheet
from mic16_qt.presentation.localization_demo import LocalizationDemoApplication,LocalizationDemoWindow
from mic16_av_fusion.fast_display_wire import (FastDisplayFrame,FastResponseFrame,FastDisplayAssembler,
    DisplayResultStore,decode,decode_direction,same_observation)
from mic16_av_fusion.hemisphere_grid import (HEMISPHERE_ALGORITHM,HEMISPHERE_GRID,LEGACY_GRID,
    raw_grid_direction,sample_hemisphere,grid_geometry)
from mic16_av_fusion.array_spherical_coordinates import array_spherical_angles
from mic16_av_fusion.spatial_response_overlay import response_rgba
from mic16_av_fusion.preview_projection import PreviewAlignment,project_direction
from test_fast_demo_alignment import observations,video_frame

OUT=ROOT/'build/verification/hemisphere_scan_20261101'

def fixture(grid=204,valid=True):
    """只用于显示边界测试的数值阵列，不作为物理图或准确率实验。"""
    az,el=raw_grid_direction(grid);now=time.monotonic()
    values=np.full((13,25),.11,np.float32);row,col=divmod(grid,25)
    values[row,col]=.6
    if col in (0,24):values[row,0]=values[row,24]=.6
    if row==12:values[row,:]=.6
    values.setflags(write=False)
    d=FastDisplayFrame(421,19,0,0,8192,5000,1000000,10,4500,valid,True,0 if valid else 2,
        grid,az,el,.6,.2,1.,None,now,algorithm=HEMISPHERE_ALGORITHM,wire_version=4)
    h=FastResponseFrame(421,19,0,0,8192,5000,1000000,10,4500,valid,True,0 if valid else 2,
        grid,values,now,algorithm=HEMISPHERE_ALGORITHM,wire_version=4)
    return d,h

class HemisphereChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=LocalizationDemoApplication.instance() or LocalizationDemoApplication([])
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mic16_wide_display_');self.folder=Path(self.temp.name);self.windows=[]
    def tearDown(self):
        for w in self.windows:w.close();w.deleteLater()
        self.app.processEvents();self.temp.cleanup()
    def window(self):
        w=LocalizationDemoWindow(trace_root=self.folder/'trace')
        w.direction.set_point_display_only(False)  # 仅检查保留的完整响应渲染器。
        w._timer.stop();w.video_panel._timer.stop();self.windows.append(w);return w

    def test_actual_C_encoded_Q24_cases_decode_and_assemble(self):
        cases=json.loads((OUT/'packets.json').read_text(encoding='utf-8'))
        for parts in cases:
            packets=[bytes.fromhex(p) for p in parts];d=decode_direction(packets[0])
            self.assertTrue(d.valid);self.assertEqual((d.wire_version,d.algorithm),(4,HEMISPHERE_ALGORITHM))
            a=FastDisplayAssembler(response_only=True);a.reset(d.boot,d.epoch)
            self.assertIsNone(a.push(packets[2],('digital',0)))
            h=a.push(packets[1],('digital',0));self.assertTrue(same_observation(d,h))
            self.assertEqual(grid_geometry(h),HEMISPHERE_GRID)
            phi,epsilon=array_spherical_angles(d.azimuth_deg,d.elevation_deg)
            self.assertAlmostEqual(epsilon,0. if d.grid<25 else 90.,places=10)
            if d.grid==0:self.assertAlmostEqual(phi,0.)
            if d.grid==6:self.assertAlmostEqual(phi,90.)
            if d.grid>=300:self.assertIsNone(phi)

    def test_version_and_grid_contract_cannot_be_mixed(self):
        data=bytearray(bytes.fromhex(json.loads((OUT/'packets.json').read_text(encoding='utf-8'))[0][0]))
        def checksum(packet):struct.pack_into('<I',packet,92,zlib.crc32(packet[96:],zlib.crc32(packet[:92])))
        for offset,fmt,value in [(4,'<H',3),(72,'<I',0xa204),(96,'<d',-20.)]:
            bad=bytearray(data);struct.pack_into(fmt,bad,offset,value);checksum(bad)
            with self.assertRaises(ValueError):decode(bytes(bad))
        data[-1]^=1
        with self.assertRaises(ValueError):decode(bytes(data))
        d,h=fixture();legacy=replace(h,algorithm=0xa204,wire_version=3)
        self.assertFalse(same_observation(d,legacy))
        store=DisplayResultStore();store.reset(d.boot,d.epoch);self.assertTrue(store.accept_current(d))
        with self.assertRaises(ValueError):store.accept_current(replace(d,sequence=1,first_sample=8192,end_sample=16384,wire_version=3,algorithm=0xa204))

    def test_every_grid_slot_roundtrips_without_sixty_degree_floor(self):
        for row in range(13):
            for col in range(25):
                phi,epsilon=array_spherical_angles(*raw_grid_direction(row*25+col))
                self.assertAlmostEqual(epsilon,row*7.5,places=10)
                if row<12:self.assertAlmostEqual(((phi-col*15+180)%360)-180,0.,places=10)
        self.assertAlmostEqual(array_spherical_angles(0.,-30.)[1],60.)
        self.assertEqual(array_spherical_angles(*raw_grid_direction(0)),(0.,0.))

    def test_response_sampling_seam_pole_and_back_are_explicit(self):
        values=np.add.outer(np.arange(13,dtype=float)*.01,np.sin(np.deg2rad(np.arange(25)*15))*.005)
        values[-1,:]=.12
        for phi,epsilon in [(0.,0.),(90.,7.5),(180.,30.),(270.,60.),(360.,0.),(17.,90.)]:
            p,e=map(math.radians,(phi,epsilon));x=math.cos(e)*math.sin(p);y=-math.cos(e)*math.cos(p);z=math.sin(e)
            value,inside=sample_hemisphere(values,x,y,z);self.assertTrue(inside)
            expected=.12 if epsilon==90 else epsilon/7.5*.01+math.sin(p)*.005
            self.assertAlmostEqual(float(value),expected,places=10)
        _,inside=sample_hemisphere(values,0.,0.,-1.);self.assertFalse(inside)

    def test_Qt_wide_table_and_rectangular_map_use_same_angles(self):
        w=self.window();d,h=fixture(204);original=h.values.tobytes()
        w._apply_fast_direction(d);w._last_heatmap=h;w._apply_fast_map();self.app.processEvents()
        self.assertEqual(w.source_table.item(0,1).text(),'60.0°');self.assertEqual(w.source_table.item(0,2).text(),'60.0°')
        self.assertEqual(w.direction._angle_limits,(0.,360.,0.,90.))
        self.assertEqual(w.direction.plot.getAxis('bottom').labelText,'方位角 / °')
        self.assertEqual(w.direction.plot.getAxis('left').labelText,'俯仰角 / °')
        np.testing.assert_array_equal(w.direction.response_samples,h.values)
        self.assertFalse(any(c.isVisible() for c in w.direction.contours));self.assertEqual(h.values.tobytes(),original)
        self.assertEqual(w.direction.plot.getViewBox().state['background'],'#00032e')
        w.direction.resize(960,540);w.direction._fit_plot()
        w.direction.plot.grab().save(str(OUT/'offline_hemisphere_rectangular_map.png'))

    def test_no_valid_direction_displays_only_pure_deep_blue(self):
        w=self.window();d,h=fixture(valid=False)
        w._apply_fast_direction(d);w._last_heatmap=h;w._apply_fast_map()
        self.assertFalse(w.direction.image.isVisible());self.assertFalse(w.direction.colorbar.isVisible())
        self.assertEqual(w.source_table.rowCount(),0)
        legacy=observations(valid=False);w._fast_frame_seen=None;w._fast_current=None
        w._apply_fast_direction(legacy[0]);w._last_heatmap=legacy[1];w._apply_fast_map()
        self.assertFalse(w.direction.image.isVisible())
        self.assertEqual(w.direction.plot.getViewBox().state['background'],'#00032e')

    def test_camera_sampling_uses_wide_contract_and_keeps_marker_small(self):
        d,h=fixture(204)
        alignment=PreviewAlignment(roll_deg=90.,horizontal_fov_deg=120.,vertical_fov_deg=100.)
        values=np.full((13,25),.7,np.float32);original=values.tobytes()
        rgba=response_rgba(values,alignment,640,360,focus_direction=(d.azimuth_deg,d.elevation_deg),
            scan_geometry=HEMISPHERE_GRID,extent_percent=50)
        point=project_direction(d.azimuth_deg,d.elevation_deg,alignment);self.assertIsNotNone(point)
        y,x=np.nonzero(rgba[:,:,3]);self.assertGreater(len(x),0)
        self.assertLess(abs(x.mean()/640-point[0]),.005);self.assertLess(abs(y.mean()/360-point[1]),.005)
        self.assertLess(x.max()-x.min(),40);self.assertEqual(values.tobytes(),original)
        for invalid in ['unknown',None]:
            with self.assertRaises(ValueError):response_rgba(values,alignment,scan_geometry=invalid)

    def test_normal_azimuth_is_undefined_without_false_scan_boundary(self):
        w=self.window();d,h=fixture(300);w._apply_fast_direction(d);w._last_heatmap=h;w._apply_fast_map()
        self.assertEqual(w.source_table.item(0,1).text(),'—');self.assertEqual(w.source_table.item(0,2).text(),'90.0°')
        self.assertNotIn('扫描边界',w.result_state.text())
        self.assertEqual(w._history[-1]['azimuth_defined'],False)

    def test_scalar_before_color_keeps_red_yellow_green_blue_and_original_samples(self):
        from mic16_av_fusion.heatmap_rendering import scalar_display_grid
        w=self.window();d,h=fixture();original=h.values.tobytes()
        w._apply_fast_direction(d);w._last_heatmap=h;w._apply_fast_map()
        image=w.direction.image;image.render()
        qimage=image.qimage.convertToFormat(QImage.Format.Format_RGB888)
        rgb=np.frombuffer(qimage.bits(),np.uint8).reshape(qimage.height(),qimage.bytesPerLine())[:,:qimage.width()*3].reshape(qimage.height(),qimage.width(),3)
        r,g,b=rgb[:,:,0],rgb[:,:,1],rgb[:,:,2]
        self.assertTrue(np.any((r>150)&(g<70)&(b<70)))
        self.assertTrue(np.any((r>150)&(g>140)&(b<70)))
        self.assertTrue(np.any((r<90)&(g>130)&(b<100)))
        self.assertTrue(np.any((r<50)&(g<110)&(b>130)))
        np.testing.assert_array_equal(image.image[4::9,4::9],h.values)
        self.assertLessEqual(np.nanmax(image.image),np.max(h.values)+1e-12)
        self.assertEqual(h.values.tobytes(),original)
        invalid=h.values.copy();invalid[0,0]=np.nan
        expanded=scalar_display_grid(invalid)
        self.assertTrue(np.isnan(expanded[4,4]));self.assertTrue(np.isfinite(expanded[13,13]))
        # 相机标记中心为红色，真实响应仍控制透明度；显示色不随分数变绿。
        values=np.full((13,25),.325,np.float32)
        alignment=PreviewAlignment(roll_deg=90.,horizontal_fov_deg=120.,vertical_fov_deg=100.)
        rgba=response_rgba(values,alignment,640,360,focus_direction=(d.azimuth_deg,d.elevation_deg),scan_geometry=HEMISPHERE_GRID)
        self.assertTrue(np.any((rgba[:,:,0]==206)&(rgba[:,:,1]==0)&(rgba[:,:,2]==0)&(rgba[:,:,3]>0)))

if __name__=='__main__':
    app=LocalizationDemoApplication([]);family=_install_cjk_font();app.setFont(QFont(family,10));app.setStyleSheet(compact_stylesheet(family))
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(HemisphereChecks))
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'host_result.json').write_text(json.dumps(dict(passed=result.wasSuccessful(),test_methods=result.testsRun,
        failures=len(result.failures),errors=len(result.errors),board_operations=0,physical_camera_opened=False,
        acoustic_accuracy_verified=False),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
