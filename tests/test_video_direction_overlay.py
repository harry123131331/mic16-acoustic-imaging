"""投影解析真值、原严格标定门、M16V/M16L本机UDP和Qt视频叠加的离线核验。"""
from pathlib import Path
from dataclasses import replace,asdict
import os,sys,json,math,time,socket,struct,zlib,hashlib
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import argparse
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--ascii-root',default='M:/');parser.add_argument('--name',default='video_overlay_check');args=parser.parse_args()
R=Path(args.ascii_root);O=R/'build/verification'/args.name;O.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(R/'host/python_tools'))
from mic16_av_fusion.preview_projection import PreviewAlignment,project_direction,profile_dict,load_profile
from mic16_av_fusion.association import Alignment,AudioObservation,Source,associate
from mic16_av_fusion.video_wire import HEADER,VideoMeta
from mic16_qt.core.models import SourceEstimate
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QFont
from mic16_qt.app import _install_cjk_font
from mic16_qt.presentation.compact_theme import compact_stylesheet
from mic16_qt.presentation.compact_window import CompactWindow
checks=0
def check(ok,why):
 global checks
 checks+=1
 if not ok:raise AssertionError(why)
def reject(action,why):
 try:action()
 except ValueError:check(True,why)
 else:check(False,why)
def wait(test,pump=lambda:None,seconds=4):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  pump()
  if test():return True
  time.sleep(.005)
 return False
p=PreviewAlignment();check(project_direction(0.,0.,p)[:2]==(.5,.5),'光轴方向落在画面中心')
for az in [-20.,-5.,5.,20.]:
 q=project_direction(az,0.,p);expected=.5+.5*math.tan(math.radians(az))/math.tan(math.radians(30))
 check(abs(q[0]-expected)<1e-14 and q[1]==.5,'水平针孔解析真值')
for el in [-15.,-5.,5.,15.]:
 q=project_direction(0.,el,p);expected=.5-.5*math.tan(math.radians(el))/math.tan(math.radians(22.5))
 check(q[0]==.5 and abs(q[1]-expected)<1e-14,'垂直向上对应像素向上')
for yaw in [-20.,0.,20.]:
 for pitch in [-10.,0.,10.]:
  q=project_direction(yaw,pitch,replace(p,yaw_deg=yaw,pitch_deg=pitch))
  check(abs(q[0]-.5)<1e-14 and abs(q[1]-.5)<1e-14,'任意中心偏航俯仰回到光轴')
base=project_direction(12.,8.,p)
check(abs(project_direction(12.,8.,replace(p,mirror_x=True))[0]-(1-base[0]))<1e-14,'水平镜像')
check(abs(project_direction(12.,8.,replace(p,mirror_y=True))[1]-(1-base[1]))<1e-14,'垂直镜像')
shift=project_direction(12.,8.,replace(p,offset_u=.05,offset_v=-.1))
check(abs(shift[0]-base[0]-.05)<1e-14 and abs(shift[1]-base[1]+.1)<1e-14,'归一化画面偏移')
for az,el in [(60.,0.),(-60.,0.),(180.,0.),(0.,60.),(0.,-60.)]:check(project_direction(az,el,p) is None,'超视野和背向不吸附到画面边缘')
for change in [dict(horizontal_fov_deg=0),dict(horizontal_fov_deg=float('nan')),dict(pitch_deg=90),dict(offset_u=1),dict(mirror_x=1),dict(radius_deg=100)]:reject(lambda c=change:project_direction(0.,0.,replace(p,**c)),'非法配置拒绝')
for v in [float('nan'),float('inf'),181.,True]:reject(lambda x=v:project_direction(x,0.,p),'非法方向拒绝')
document=profile_dict(p);check(load_profile(json.loads(json.dumps(document)))==p,'参数JSON往返')
bad=dict(document);bad['calibrated']=True;reject(lambda:load_profile(bad),'粗对齐文件不能声明已精确标定')
# 原严格关联器的未核验时间门保持，预览不把它改为通过。
meta=VideoMeta(19,23,1000000,64,48,1,0,1,2,3,6144,0,4)
audio=AudioObservation(19,0,900000,1050000,3,False,(Source(1,0.,0.),))
alignment=Alignment(64,48,1,2,4,(32.,.5,0.),(24.,0.,-.5),'REAL_APPROXIMATE_ALIGNMENT',9)
check(associate(audio,meta,alignment,(),boxes_frame_id=23,now_us=1100000)[0]['status']=='TIME_UNVERIFIED','原严格物理同步检查保持')
app=QApplication.instance() or QApplication([]);family=_install_cjk_font();app.setFont(QFont(family,10));app.setStyleSheet(compact_stylesheet(family))
window=CompactWindow();window.show();app.processEvents();panel=window.video_panel
source_port=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
source_port.bind(('127.0.0.1',0));port=source_port.getsockname()[1];source_port.close()
check(window.start_source_udp('127.0.0.1',port,'127.0.0.1',O/'source_session',evidence_source='LOCAL_UDP_TEST'),'原逐源网络入口能启动')
check(wait(lambda:window._source_udp_worker.bound_port==port,app.processEvents),'逐源socket已绑定')
vport=panel.start_video('127.0.0.1',0,'127.0.0.1');receiver=panel.receiver
fixture=R/'build/verification/integrated_av_checked_20261020/c_video_packets.bin';raw=fixture.read_bytes();packets=[];at=0
while at<len(raw):n=struct.unpack_from('<I',raw,at)[0];at+=4;packets.append(raw[at:at+n]);at+=n
check(len(packets)==7,'复用原已通过C视频分片，不制造新协议真值')
sources=[(1,0.,0.,.8),(2,15.,8.,.9)]
body=b''.join(struct.pack('<I3d',*s) for s in sources)
head=struct.pack('<4sHHIIQQI',b'M16L',1,len(sources),20261019,0,0,40960,0)
direction=head[:32]+struct.pack('<I',zlib.crc32(head[:32]+body))+body
try:
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
  for packet in packets:sender.sendto(packet,('127.0.0.1',vport))
  check(wait(lambda:panel.last_frame is not None,app.processEvents),'M16V→原视频页完整帧')
  sender.sendto(direction,('127.0.0.1',port))
  check(wait(lambda:window.source_table.rowCount()==2,app.processEvents),'M16L→原逐源接收→声源表')
  check(not panel.overlay_enabled.isChecked() and panel.canvas.markers==(),'正常入口默认不叠加未粗对齐方向')
  panel.overlay_enabled.setChecked(True);app.processEvents()
  check(len(panel.canvas.markers)==2 and panel.canvas.markers[0][:2]==(.5,.5),'真正接收的声学方向进入视频标记')
  check('未核验同步' in panel.canvas.message and 'LOCAL_UDP_TEST' in panel.detail.text(),'预览不冒充物理同步验收')
  check(panel.canvas.markers[0][-1]=='ID1' and panel.canvas.markers[1][-1]=='ID2','原声源ID保持')
  frame=panel.last_frame
  panel.last_frame=replace(frame,received_monotonic_s=time.monotonic()-2);panel._update_overlay()
  check(not panel.canvas.markers,'视频过期清除标记')
  panel.last_frame=replace(frame,received_monotonic_s=time.monotonic());panel._acoustic_received=time.monotonic()-6;panel._update_overlay()
  check(not panel.canvas.markers,'声学结果过期清除标记')
  panel._acoustic_received=time.monotonic();panel._update_overlay();check(len(panel.canvas.markers)==2,'新鲜状态可恢复')
  panel.last_frame=replace(frame,meta=replace(frame.meta,transform_id=99),received_monotonic_s=time.monotonic());panel._update_overlay()
  check(not panel.canvas.markers and '几何配置改变' in panel.canvas.message,'镜像裁剪/图像模式变更不继续用旧映射')
  panel.last_frame=replace(frame,received_monotonic_s=time.monotonic());panel._overlay_binding=None
  panel.set_acoustic_sources([SourceEstimate('ID3',60.,0.,0.,'CURRENT')],window=0)
  check(not panel.canvas.markers,'视野外方向保留声学角度但不画到边界')
  panel.set_acoustic_sources([SourceEstimate('ID4',0.,0.,0.,'HELD')],window=0);check(not panel.canvas.markers,'保持轨迹不冒充当前检出')
  panel.set_acoustic_sources([],window=0);check(not panel.canvas.markers,'零个当前声源时不画点')
 check(window.disconnect_source(),'断开源会话成功')
 check(not panel._acoustic_sources,'断开后清空旧声学方向')
 window.close();check(wait(lambda:not window.isVisible(),app.processEvents),'正常退出视频与源接收线程')
 check(not receiver.snapshot()['running'],'视频socket释放')
finally:
 if panel.receiver:panel.receiver.close(1)
 if window._source_udp_worker:window._source_udp_worker.stop()
 window.close();app.processEvents()
result=dict(status='PASS_APPROXIMATE_PROJECTION_M16L_M16V_QT_OVERLAY',checks=checks,
 strict_association_unchanged=True,default_overlay_enabled=False,real_socket_directions_and_video=True,
 stale_source_hidden=True,stale_video_hidden=True,outside_view_not_clamped=True,held_tracks_not_drawn=True,
 physical_AV_sync_verified=False,precise_camera_calibration=False,distance_estimated=False,
 board_operations=0,training_runs=0,calibration_profile=profile_dict(p),source_fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest())
(O/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,ensure_ascii=False))
