"""独立热图UDP监听：两个待组装窗，一个最新完整图，不阻塞音频保存或GUI。"""
from __future__ import annotations
import ipaddress
import socket
import threading
from .heatmap_wire import HeatmapAssembler

class HeatmapReceiver:
    def __init__(self,listen_ip='0.0.0.0',port=5003,board_ip='192.168.1.10',*,notify_ready=None):
        if notify_ready is not None and not callable(notify_ready):raise ValueError('热图通知回调无效')
        self.notify_ready=notify_ready
        self.listen_ip=str(ipaddress.IPv4Address(listen_ip));self.board_ip=str(ipaddress.IPv4Address(board_ip))
        if type(port)is not int or not 0<=port<=65535:raise ValueError('热图端口无效')
        self.port=port;self.bound_port=None;self._lock=threading.Lock();self._stop=threading.Event()
        self._socket=self._thread=self._latest=None
        self._display_binding=None
        self._stats=dict(running=False,error='',ignored_peer=0,replaced=0,packets=0,pending_payload_bytes=0)

    def begin_display_epoch(self,boot,epoch):
        if not isinstance(boot,int) or not 0<boot<=0xffffffff or not isinstance(epoch,int) or not 0<epoch<=0xffffffff:raise ValueError('空间图会话身份无效')
        with self._lock:
            if self._display_binding is not None and self._display_binding[0]==boot and epoch<self._display_binding[1]:return
            if self._display_binding!=(boot,epoch):self._display_binding=(boot,epoch);self._latest=None

    def start(self):
        if self._thread is not None:raise RuntimeError('新热图会话需要新接收器')
        sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        try:sock.bind((self.listen_ip,self.port));sock.settimeout(.15)
        except BaseException:sock.close();raise
        self._socket=sock;self.bound_port=sock.getsockname()[1];self._stats['running']=True
        self._thread=threading.Thread(target=self._run,name='MIC16-heatmap',daemon=False)
        try:self._thread.start()
        except BaseException:sock.close();self._stats['running']=False;raise
        return self.bound_port

    def _run(self):
        assembler=HeatmapAssembler();binding=None
        from .fast_display_wire import FastDisplayAssembler
        try:
            while not self._stop.is_set():
                with self._lock:wanted=self._display_binding
                if wanted is not None and wanted!=binding:
                    assembler=FastDisplayAssembler(response_only=True);assembler.reset(*wanted);binding=wanted
                try:data,peer=self._socket.recvfrom(2048)
                except socket.timeout:
                    assembler.expire()
                    with self._lock:self._stats['pending_payload_bytes']=assembler.pending_payload_bytes
                    continue
                except OSError:
                    if self._stop.is_set():break
                    raise
                if peer[0]!=self.board_ip:
                    with self._lock:self._stats['ignored_peer']+=1
                    continue
                with self._lock:wanted=self._display_binding
                if wanted is not None and wanted!=binding:
                    assembler=FastDisplayAssembler(response_only=True);assembler.reset(*wanted);binding=wanted
                try:frame=assembler.push(data,peer)
                except ValueError:frame=None
                notify=False
                with self._lock:
                    self._stats['packets']+=1;self._stats.update(assembler.stats)
                    self._stats['pending_payload_bytes']=assembler.pending_payload_bytes
                    if frame is not None and not self._stop.is_set():
                        notify=self._latest is None
                        if self._latest is not None:self._stats['replaced']+=1
                        self._latest=frame
                # 锁外只发Qt排队信号，接收线程不直接访问窗口；一个最新槽至多一个通知。
                if notify and self.notify_ready is not None:self.notify_ready()
        except Exception as exc:
            with self._lock:self._stats['error']=str(exc)
        finally:
            self._socket.close()
            with self._lock:self._stats['running']=False;self._stats['pending_payload_bytes']=0

    def take_latest(self):
        with self._lock:frame,self._latest=self._latest,None;return frame

    def snapshot(self):
        with self._lock:return dict(self._stats,latest_frames=int(self._latest is not None),bound_port=self.bound_port)

    def close(self,timeout=0):
        self._stop.set()
        if self._socket is not None:self._socket.close()
        if self._thread is not None:self._thread.join(max(0,timeout))
        stopped=self._thread is None or not self._thread.is_alive()
        if stopped:
            with self._lock:self._latest=None
        return stopped
