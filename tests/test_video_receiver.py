"""真实本机UDP与GUI回归；图像由C固定测试数据产生，不是摄像头实测。"""
from pathlib import Path
import argparse,json,os,socket,struct,sys,time,zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host/python_tools'))
from mic16_av_fusion.video_receiver import VideoReceiver
from mic16_av_fusion.video_wire import HEADER
checks=0

def check(ok,why):
    global checks
    checks+=1
    if not ok:raise AssertionError(why)

def wait_for(predicate,pump=lambda:None,seconds=3):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        pump()
        if predicate():return True
        time.sleep(.005)
    return False

def change(packet,frame=None,epoch=None):
    fields=list(HEADER.unpack_from(packet))
    if frame is not None:fields[4]=frame
    if epoch is not None:fields[3]=epoch
    head=HEADER.pack(*fields);fields[-1]=zlib.crc32(packet[72:],zlib.crc32(head[:68]))&0xffffffff
    return HEADER.pack(*fields)+packet[72:]

def send(sender,port,packets,frame=23,epoch=19):
    for packet in packets:
        sender.sendto(change(packet,frame,epoch),('127.0.0.1',port))

def receiver_checks(packets):
    receiver=VideoReceiver('127.0.0.1',0,'127.0.0.1')
    try:
        port=receiver.start()
        check(port>0 and receiver.snapshot()['running'],'监听真实socket')
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            sender.sendto(b'bad packet',('127.0.0.1',port))
            check(wait_for(lambda:receiver.snapshot()['rejected']==1),'坏包不使线程退出')
            send(sender,port,[packets[i] for i in (6,0,2,2,1,5,4,3)])
            check(wait_for(lambda:receiver.snapshot()['completed']==1),'C分片乱序重组')
            first=receiver.take_latest()
            check(first is not None and first.pixels==bytes((i*37+11)&255 for i in range(6144)),'C真值逐字节一致')
            check(receiver.snapshot()['duplicates']==1,'重复包计数')
            check(receiver.take_latest() is None,'同一完整帧只消费一次')
            # GUI故意不取帧：所有帧仍完成校验，但最新槽最多一个。
            for identity in range(24,28):
                send(sender,port,packets,identity)
                check(wait_for(lambda:receiver.snapshot()['completed']==identity-22),'后续完整帧到达')
                check(receiver.snapshot()['latest_frames']==1 and receiver.snapshot()['latest_payload_bytes']==6144,'最新队列有界')
            check(receiver.snapshot()['replaced']==3,'慢消费者替换旧帧而非累积')
            check(receiver.take_latest().meta.frame_id==27,'消费者获得最新完整帧')
            send(sender,port,packets,28,20)
            check(wait_for(lambda:receiver.snapshot()['rejected']>=8),'epoch变化须重启会话')
            check(receiver.snapshot()['completed']==5 and receiver.take_latest() is None,'不混用另一epoch')
            send(sender,port,packets[:-1],28)
            check(wait_for(lambda:receiver.snapshot()['pending_payload_bytes']==6144),'不完整帧只占有界组帧区')
            check(receiver.take_latest() is None,'不发布缺片图像')
            for identity in range(29,35):
                send(sender,port,packets[:1],identity)
                check(wait_for(lambda:receiver.snapshot()['packets']>=identity-28+6+36),'连续不完整帧得到处理')
                check(receiver.snapshot()['pending_payload_bytes']<=2*6144,'只留两帧组装区')
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as other:
                send(other,port,packets,40)
                check(wait_for(lambda:receiver.snapshot()['rejected']>=15),'会话期间发送端口改变拒收')
            conflict=VideoReceiver('127.0.0.1',port,'127.0.0.1')
            try:
                conflict.start()
            except OSError:check(True,'重复监听端口被拒绝')
            else:
                conflict.close(1);check(False,'不得复用视频端口')
    finally:
        check(receiver.close(1),'停止线程并回收socket')
    check(not receiver.snapshot()['running'] and receiver.snapshot()['latest_frames']==0,'退出清空最新帧')
    try:receiver.start()
    except RuntimeError:check(True,'旧实例不可复用')
    else:check(False,'旧实例不可复用')
    restarted=VideoReceiver('127.0.0.1',port,'127.0.0.1')
    try:
        restarted.start()
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            send(sender,port,packets,1,20)
            check(wait_for(lambda:restarted.snapshot()['completed']==1),'端口回收及新epoch重连')
            check(restarted.take_latest().meta.epoch==20,'重连清除旧会话限制')
    finally:check(restarted.close(1),'重连线程回收')
    filtered=VideoReceiver('127.0.0.1',0,'127.0.0.2')
    try:
        p=filtered.start()
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:send(sender,p,packets)
        check(wait_for(lambda:filtered.snapshot()['ignored_peer']==7),'板卡IP过滤')
        check(filtered.snapshot()['packets']==0 and filtered.snapshot()['completed']==0,'错误IP不能占用组帧')
    finally:check(filtered.close(1),'过滤线程回收')
    return dict(complete_frames=5,latest_frame_slots=1,partial_frame_slots=2,fixture_bytes=6144)

def gui_checks(packets,out):
    os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
    from dataclasses import replace
    from PySide6.QtWidgets import QApplication
    from PySide6.QtGui import QFont
    from mic16_qt.app import _install_cjk_font
    from mic16_qt.presentation.compact_window import CompactWindow
    from mic16_qt.presentation.compact_theme import compact_stylesheet
    app=QApplication([]);font=_install_cjk_font();app.setFont(QFont(font,10));app.setStyleSheet(compact_stylesheet(font))
    window=CompactWindow();window.show();app.processEvents()
    panel=window.video_panel
    try:
        check(panel.receiver is None and window._mode=='idle' and not window._active,'启动不自动监听/演示')
        check(window.primary_tabs.currentIndex()==0 and window.primary_tabs.count()==2,'默认定位为主，视频单独一页')
        check('mic16_qt.presentation.main_window' not in sys.modules,'不加载旧七页研究界面')
        check(window.audio_tabs.count()==2 and window.source_kind_combo.count()==2,'波形频谱与逐源增强/WDAS控件保留')
        window.primary_tabs.setCurrentWidget(panel)
        port=panel.start_video('127.0.0.1',0,'127.0.0.1')
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            send(sender,port,packets)
            check(wait_for(lambda:panel.last_frame is not None,app.processEvents),'GUI线程取得真实socket最新帧')
            check(panel.last_frame.meta.frame_id==23 and panel.canvas.image.width()==64 and panel.canvas.image.height()==48,'图像尺寸与帧号')
            raw=11+48*256;color=panel.canvas.image.pixelColor(0,0)
            red=(raw>>11)&31;green=(raw>>5)&63;blue=raw&31
            check((color.red(),color.green(),color.blue())==((red<<3)|(red>>2),(green<<2)|(green>>4),(blue<<3)|(blue>>2)),'独立RGB565像素真值')
            check(window.source_table.rowCount()==0 and window._mode=='idle','视频不伪造声源或启动音频')
            check('叠加待' in panel.canvas.message,'未核验几何时不画声源点')
            panel.canvas.message='离线协议测试 · 合成图像，非摄像头实测'
            panel.canvas.update();app.processEvents()
            check(window.save_screenshot(out/'compact_video.png'),'视频页截图')
            panel.last_frame=replace(panel.last_frame,received_monotonic_s=time.monotonic()-2)
            panel._poll();check('已过期' in panel.canvas.message,'过期画面明确标记')
            send(sender,port,packets,24)
            check(wait_for(lambda:panel.last_frame is not None and panel.last_frame.meta.frame_id==24,app.processEvents),'新帧解除过期状态')
            check('已接收' in panel.canvas.message,'恢复当前视频状态')
        receiver=panel.receiver
        check(wait_for(panel.shutdown,app.processEvents),'非阻塞停止视频')
        check(panel.receiver is None and panel.canvas.image.isNull() and panel.last_frame is None,'停止后清理图像与会话')
        # 原声学演示路径仍可独立运行，并明确保持其非实测状态。
        window.start_demo();app.processEvents();window._refresh()
        before=(window._mode,window._active,window.source_table.rowCount())
        check(before[0]=='synthetic' and before[1],'原定位演示仍可运行')
        panel.start_video('127.0.0.1',0,'127.0.0.1')
        check((window._mode,window._active,window.source_table.rowCount())==before,'视频开关不改变声学状态')
        # 不显式停止，直接关闭主窗口也必须回收视频线程。
        active_receiver=panel.receiver
        window.close()
        check(wait_for(lambda:not window.isVisible(),app.processEvents),'主窗口关闭不会阻塞')
        check(not active_receiver.snapshot()['running'] and panel.receiver is None,'关闭主窗口回收视频线程')
        check(not receiver.snapshot()['running'],'首次线程保持退出')
    finally:
        if panel.receiver is not None:panel.receiver.close(1)
        window.close();app.processEvents()
    return dict(screenshot='compact_video.png',camera_pixels=False,synthetic_fixture_label=True,
                no_fake_source_from_video=True,no_research_ui=True,window_close_thread_reclaimed=True)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=a.fixture.read_bytes();packets=[];offset=0
    while offset<len(data):
        n=struct.unpack_from('<I',data,offset)[0];offset+=4;packets.append(data[offset:offset+n]);offset+=n
    check(len(packets)==7 and offset==len(data),'C分片fixture有效')
    result=receiver_checks(packets)
    result['gui']=gui_checks(packets,a.output.parent)
    result.update(status='PASS_C_UDP_VIDEO_RECEIVER_AND_COMPACT_GUI',checks=checks,board_operations=0,
                  physical_AV_sync=False,real_visual_detector=False,board_video_sender=False)
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False))
if __name__=='__main__':main()
