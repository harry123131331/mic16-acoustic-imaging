"""用独立合成点验证标定流程；合成结果不得导出为真实现场配置。"""
from pathlib import Path
import importlib.util,json,tempfile,unittest
from types import SimpleNamespace
import numpy as np
import cv2

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('camera_calibration',ROOT/'scripts/calibrate_camera.py')
cal=importlib.util.module_from_spec(spec);spec.loader.exec_module(cal)

class CameraCalibrationTests(unittest.TestCase):
    def setUp(self):
        base=ROOT/'build/test_camera';base.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=base)
        if not Path(self.temp.name).resolve().is_relative_to(base.resolve()):raise ValueError('临时目录越界')
        self.addCleanup(self.temp.cleanup)
        self.out=Path(self.temp.name)
        self.k=np.array([[600.,0,400],[0,610,240],[0,0,1]])
        self.d=np.array([-.04,.01,.001,-.001,0.])
        self.r=np.array([.03,-.02,.01]);self.t=np.array([.02,-.07,-.10])
        self.camera=dict(origin='SYNTHETIC_TEST',image_size=[800,480],K=self.k.tolist(),D=self.d.tolist())
        self.intr=self.out/'intrinsics.json';self.intr.write_text(json.dumps(self.camera))
    def points(self,name,seed):
        random=np.random.default_rng(seed)
        a=random.uniform([-.6,-.3,1.],[.6,.3,3.],(30,3))
        b,_=cv2.projectPoints(a,self.r,self.t,self.k,self.d)
        file=self.out/name
        file.write_text(json.dumps(dict(origin='SYNTHETIC_TEST',array_points_m=a.tolist(),image_points_px=b.reshape(-1,2).tolist())))
        return file
    def fitted(self):
        path=self.out/'fitted.json'
        cal.extrinsics(SimpleNamespace(intrinsics=self.intr,points=self.points('fit.json',1),
            ransac_px=3,mount_id=1,calibration_id=7,output=path))
        return path
    def test_pose_and_independent_reprojection(self):
        fitted=self.fitted();target=self.out/'verified.json'
        cal.verify(SimpleNamespace(calibration=fitted,points=self.points('heldout.json',2),max_p95_px=.01,output=target))
        c=cal.load(target)
        self.assertTrue(c['geometry_verified']);self.assertLess(c['validation']['p95_px'],.01)
        rotation,_=cv2.Rodrigues(self.r)
        np.testing.assert_allclose(c['R'],rotation,atol=1e-6)
        np.testing.assert_allclose(c['t_m'],self.t,atol=1e-6)
        with self.assertRaises(ValueError):cal.export(SimpleNamespace(calibration=target,output=self.out/'site.h'))
    def test_fit_data_cannot_validate_itself(self):
        fitted=self.fitted()
        with self.assertRaises(ValueError):cal.verify(SimpleNamespace(calibration=fitted,points=self.out/'fit.json',max_p95_px=1,output=self.out/'wrong.json'))
    def test_failed_heldout_is_preserved(self):
        fitted=self.fitted();path=self.points('bad_heldout.json',3);o=cal.load(path)
        o['image_points_px']=(np.asarray(o['image_points_px'])+[20,0]).tolist();path.write_text(json.dumps(o))
        target=self.out/'failed.json'
        with self.assertRaises(ValueError):cal.verify(SimpleNamespace(calibration=fitted,points=path,max_p95_px=1,output=target))
        self.assertFalse(cal.load(target)['geometry_verified'])
    def test_rgb565_and_exact_frame_length(self):
        p=self.out/'frame.bin';raw=np.full((480,800),0xf800,dtype='<u2');raw.tofile(p)
        target=self.out/'red.png';cal.decode(SimpleNamespace(input=p,output=target,byte_order='little'))
        image=cv2.imdecode(np.frombuffer(target.read_bytes(),np.uint8),cv2.IMREAD_COLOR);np.testing.assert_equal(image[0,0],[0,0,255])
        p.write_bytes(b'bad')
        with self.assertRaises(ValueError):cal.decode(SimpleNamespace(input=p,output=self.out/'wrong.png',byte_order='little'))
    def test_no_overwrite(self):
        with self.assertRaises(FileExistsError):cal.save(self.intr,{})
    def test_intrinsics_from_synthetic_checkerboards(self):
        images=self.out/'checkerboards';images.mkdir()
        # 从已知三维棋盘格投影生成图像，单独检验角点检测与内参求解入口。
        board=np.zeros((700,1000),np.uint8)
        for row in range(7):
            for col in range(10):board[row*100:(row+1)*100,col*100:(col+1)*100]=240 if (row+col)%2 else 20
        random=np.random.default_rng(5);corners=np.array([[0,0,0],[.25,0,0],[.25,.175,0],[0,.175,0]],float)
        for i in range(16):
            rv=random.uniform([-.35,-.4,-.12],[.35,.4,.12]);tv=np.array([-.125,-.0875,random.uniform(.5,.8)])
            pixels,_=cv2.projectPoints(corners,rv,tv,self.k,np.zeros(5))
            transform=cv2.getPerspectiveTransform(np.array([[0,0],[999,0],[999,699],[0,699]],np.float32),pixels.reshape(4,2).astype(np.float32))
            frame=cv2.warpPerspective(board,transform,(800,480),borderValue=170)
            ok,encoded=cv2.imencode('.png',frame);self.assertTrue(ok)
            (images/f'board_{i:02d}.png').write_bytes(encoded.tobytes())
        output=self.out/'solved_intrinsics.json'
        cal.intrinsics(SimpleNamespace(images=images,rows=6,cols=9,square_mm=25,
            focus_id='synthetic_fixed',origin='SYNTHETIC_TEST',output=output))
        result=cal.load(output)
        self.assertGreaterEqual(len(result['images']),10)
        np.testing.assert_allclose(np.diag(result['K'])[:2],np.diag(self.k)[:2],rtol=.03)
        self.assertFalse(result['geometry_verified'])

if __name__=='__main__':unittest.main(verbosity=2)
