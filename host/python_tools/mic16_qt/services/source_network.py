"""M16S/M16L独立网络会话，不导入检测器，也不混用旧UDP。套接字所有者同步组装并保存整窗，磁盘提交后才给GUI有界预览；接收到协议不代表硬件验收。"""
from __future__ import annotations
import hashlib
import importlib.util
import ipaddress
import json
import math
from pathlib import Path
import socket
import select
import struct
import sys
import threading
import time
import uuid
import zlib
import numpy as np
from PySide6.QtCore import QObject, Signal, Slot

def _wire():
    name='_mic16_frozen_M16S_wire'
    if name not in sys.modules:
        path=Path(__file__).resolve().parents[3]/'release/pc_incremental_c16_frozen_20261015/software/pc_per_source/source_wire.py'
        spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec)
        sys.modules[name]=module;spec.loader.exec_module(module)
    return sys.modules[name]

def directions(datagram):
    if not isinstance(datagram,bytes) or not 36<=len(datagram)<=260:raise ValueError('M16L长度错误')
    magic,version,count,epoch,window,begin,end,checksum=struct.unpack_from('<4sHHIIQQI',datagram)
    if magic!=b'M16L' or version!=1 or count>8 or not epoch or len(datagram)!=36+count*28:
        raise ValueError('M16L版本／源数／长度错误')
    if (begin,end)!=(window*40960,(window+1)*40960) or zlib.crc32(datagram[:32]+datagram[36:])!=checksum:
        raise ValueError('M16L时间区间／CRC错误')
    sources=[];seen=set()
    for i in range(count):
        identity,az,el,score=struct.unpack_from('<I3d',datagram,36+i*28)
        if not identity or identity in seen or not all(map(math.isfinite,[az,el,score])) or not(-90<az<90 and -90<el<90 and .65<=score<=1):
            raise ValueError('M16L编号／方向／分数错误')
        seen.add(identity);sources.append(dict(source_id=identity,azimuth_deg=az,elevation_deg=el,score=score))
    return dict(epoch=epoch,window=window,input_start_sample=begin,input_end_sample_exclusive=end,
        sample_rate_numerator=78125,sample_rate_denominator=2,sources=sources,
        timestamp_domain='relative_input_audio_clock_not_camera_calibrated',evidence_source='UDP_RECEIVED_UNVERIFIED')

def _json(path,data):
    temp=path.with_suffix(path.suffix+'.pending')
    temp.write_text(json.dumps(data,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    temp.replace(path)

class NetworkWaveWriter:
    """显式指定输出目录，未完成会话不标为完成。全部ID及音频类型均保存为Float32 WAV，不受当前预览选择影响；只有完整源窗提交后才给缺席声源补非活动时间。不伪造MIC0，也不为退役声源保留常开句柄。"""
    def __init__(self,directory,*,evidence_source='UDP_RECEIVED_UNVERIFIED',max_source_ids=128,max_samples=9600000):
        self.directory=Path(directory).resolve();self.directory.mkdir(parents=True,exist_ok=False)
        self.evidence_source=evidence_source;self.segment_id=self.directory.name;self.pose_key='BOARD_POSE_UNVERIFIED'
        self.max_source_ids=max_source_ids;self.max_samples=max_samples;self.ids=set();self.next_sample=0;self.windows=0
        self.localization_only=False
        self.closed=False;self.final=False;self.epoch=None;self.zero=np.zeros(4096,dtype='<f4')
        self.events=(self.directory/'packets.jsonl').open('x',encoding='utf-8')
        self.locations=(self.directory/'directions.jsonl').open('x',encoding='utf-8');self.location_count=0
        _json(self.directory/'session.json',self._session(False,'receiving'))

    def _session(self,ok,error):
        return dict(schema_version=2,status='COMPLETED' if ok else 'STOPPED_LOCALIZATION_ONLY' if self.localization_only and not error else 'INCOMPLETE',segment_id=self.segment_id,pose_key=self.pose_key,
            evidence_source=self.evidence_source,audio_rate=16000,output_samples=self.next_sample,packets=self.windows,
            audio_files=len(self.ids)*2,activity_metadata='packets.jsonl',audio_manifest='audio_files.jsonl',
            direction_metadata='directions.jsonl',direction_packets=self.location_count,epoch=self.epoch,
            error=str(error),zero_padding_means_inactive_source_not_captured_sound=True,
            missing_transport_audio_filled=False,input_MIC0_present=False,board_execution_verified=False,
            physical_timestamp_verified=False,maximum_source_ids=self.max_source_ids,maximum_samples=self.max_samples)

    @staticmethod
    def _header(samples):
        size=samples*4
        return struct.pack('<4sI4s4sIHHIIHH4sI',b'RIFF',36+size,b'WAVE',b'fmt ',16,3,1,16000,64000,4,32,b'data',size)

    def _append(self,path,begin,values):
        if not path.exists():path.write_bytes(self._header(0))
        with path.open('r+b') as f:
            header=f.read(44);current=path.stat().st_size-44
            if len(header)!=44 or current<0 or current%4 or header!=self._header(current//4) or current//4>begin:
                raise ValueError('WAV重复／截断／时间轴错误')
            f.seek(0,2);missing=begin-current//4
            while missing:
                n=min(missing,len(self.zero));f.write(self.zero[:n].tobytes());missing-=n
            array=np.asarray(values,'<f4');f.write(array.tobytes());f.seek(0);f.write(self._header(begin+len(array)));f.flush()

    def direction(self,event):
        if self.closed:raise RuntimeError('逐源会话已关闭')
        self.locations.write(json.dumps(event,ensure_ascii=False,allow_nan=False)+'\n');self.locations.flush();self.location_count+=1

    def audio(self,event):
        if self.closed:raise RuntimeError('逐源会话已关闭')
        begin,end=event['output_start_sample'],event['output_end_sample_exclusive']
        if self.final or begin!=self.next_sample or end<=begin or end>self.max_samples or event['output_rate']!=16000:
            raise ValueError('逐源音频不连续／超出本次保存上限')
        if self.epoch is not None and event['epoch']!=self.epoch:raise ValueError('会话epoch变更')
        tracks=event['tracks'];new_ids=self.ids|set(tracks)
        if len(new_ids)>self.max_source_ids:raise ValueError('本次录制累计ID超过上限，请另起会话')
        for identity,s in tracks.items():
            if type(identity)is not int or identity<=0:raise ValueError('源ID错误')
            for kind in ['WDAS','enhanced']:
                values=np.asarray(s[kind])
                if values.shape!=(end-begin,) or not np.isfinite(values).all():raise ValueError('音频长度或数值错误')
        for identity in sorted(new_ids):
            for kind in ['WDAS','enhanced']:
                values=tracks[identity][kind] if identity in tracks else self.zero[:0]
                path=self.directory/f'source_{identity:04d}_{kind}.wav'
                self._append(path,begin,values)
                if identity not in tracks:self._append(path,end,self.zero[:0])
        row=dict(schema_version=2,segment_id=self.segment_id,pose_key=self.pose_key,epoch=event['epoch'],window=event['window'],
            output_start_sample=begin,output_end_sample_exclusive=end,output_rate=16000,final=event['final'],
            current_confirmed=[dict(source_id=i) for i,s in sorted(tracks.items()) if s['current_confirmed']],
            audio_IDs=sorted(tracks),transition_audio_is_not_current_detection=True,evidence_source=self.evidence_source)
        self.events.write(json.dumps(row,ensure_ascii=False)+'\n');self.events.flush()
        self.ids=new_ids;self.epoch=event['epoch'];self.next_sample=end;self.windows+=1;self.final=event['final']

    def close(self,*,completed,error=''):
        if self.closed:return self.result
        self.closed=True;self.events.close();self.locations.close()
        ok=bool(completed and self.final and self.windows and self.location_count==self.windows)
        if ok:
            with (self.directory/'audio_files.jsonl').open('x',encoding='utf-8') as f:
                for identity in sorted(self.ids):
                    for kind in ['WDAS','enhanced']:
                        path=self.directory/f'source_{identity:04d}_{kind}.wav'
                        digest=hashlib.sha256()
                        with path.open('rb') as stream:
                            for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
                        f.write(json.dumps(dict(path=path.name,source_id=identity,kind=kind,frames=self.next_sample,
                            sample_rate=16000,sha256=digest.hexdigest()))+'\n')
        self.result=self._session(ok,error);_json(self.directory/'session.json',self.result);return self.result

class NetworkPreview:
    """只保留计数，音频预览由工作线程截为末2048点；不落盘、不补缺失数据。"""
    def __init__(self):
        self.localization_only=False
        self.closed=False;self.final=False;self.windows=0;self.location_count=0
        self.next_sample=0;self.epoch=None;self.result=None;self.first_window=None;self.first_audio_sample=None

    def direction(self,event):
        if self.closed:raise RuntimeError('预览已关闭')
        if self.first_window is None:self.first_window=event['window']
        self.location_count+=1

    def audio(self,event):
        if self.closed:raise RuntimeError('预览已关闭')
        begin,end=event['output_start_sample'],event['output_end_sample_exclusive']
        if self.epoch is None and not self.windows:
            self.first_audio_sample=begin;self.next_sample=begin
        if self.final or begin!=self.next_sample or end<=begin or event['output_rate']!=16000:
            raise ValueError('预览音频不连续或采样率错误')
        if self.epoch is not None and event['epoch']!=self.epoch:raise ValueError('预览epoch改变')
        for identity,track in event['tracks'].items():
            if type(identity)is not int or identity<=0:raise ValueError('预览源ID错误')
            for kind in ('WDAS','enhanced'):
                a=np.asarray(track[kind])
                if a.shape!=(end-begin,) or not np.isfinite(a).all():raise ValueError('预览音频长度或数值错误')
        self.next_sample=end;self.windows+=1;self.epoch=event['epoch'];self.final=event['final']

    def close(self,*,completed,error=''):
        if self.closed:return self.result
        self.closed=True
        ok=bool(completed and self.final and self.windows and self.location_count==self.windows)
        self.result=dict(status='COMPLETED' if ok else 'STOPPED' if not error else 'INCOMPLETE',
            recording_enabled=False,audio_files_saved=0,joined_at_window=self.first_window,first_audio_sample=self.first_audio_sample,packets=self.windows,direction_packets=self.location_count,
            output_samples=self.next_sample,epoch=self.epoch,error=str(error),board_execution_verified=False)
        return self.result

class SourceNetworkSession:
    """每会话只绑定一个端点/时段，保存有界对齐元数据，音频按事务提交。"""
    def __init__(self,writer):
        self.localization_only=False
        self.allow_late_join=isinstance(writer,NetworkPreview);self.first_window=0;self.ignored_before_join=0
        self.writer=writer;self.peer=None;self.epoch=None;self.audio=None
        self.next_direction=0;self.last_direction=None;self.metadata={};self.last_audio_window=-1
        self.closed=self.failed=False;self.packets=0;self.ignored_endpoints=0

    def set_localization_only(self,enabled):
        enabled=bool(enabled)
        if enabled==self.localization_only:return
        if self.last_audio_window>=0:raise ValueError('已接收增强音频，不能中途切成仅定位')
        self.localization_only=enabled;self.metadata.clear()
        if hasattr(self.writer,'localization_only'):self.writer.localization_only=enabled

    def push(self,data,peer):
        if self.closed or self.failed:raise RuntimeError('逐源网络段已结束')
        if self.peer is not None and peer!=self.peer:
            self.ignored_endpoints+=1;return None
        try:
            if data[:4]==b'M16L':event=directions(data);kind='directions'
            elif data[:4]==b'M16S':
                if self.localization_only:raise ValueError('仅定位模式不接受板端增强音频')
                event=_wire().decode(data);kind='audio'
            else:raise ValueError('不是M16S／M16L协议，不能接到旧端口')
            epoch=event['epoch'] if kind=='directions' else event.epoch
            if self.epoch is None:
                # 预览从首次完整方向清单接入；不把此前未接收区间补成零。
                if self.allow_late_join and kind!='directions':
                    self.ignored_before_join+=1;return None
                self.first_window=event['window'] if self.allow_late_join else 0
                self.next_direction=self.first_window
                self.epoch=epoch;self.peer=peer
            if epoch!=self.epoch:raise ValueError('板端epoch改变，请另起会话')
            self.packets+=1
            if self.allow_late_join and kind=='audio' and event.window<self.first_window:
                self.ignored_before_join+=1;return None
            if kind=='directions':
                digest=hashlib.sha256(data).digest();index=event['window']
                if index==self.next_direction-1 and self.last_direction==digest:return None
                if index!=self.next_direction:raise ValueError('方向结果缺窗或冲突')
                if not self.localization_only and len(self.metadata)>=3:raise ValueError('方向数据超过3窗等待上限')
                self.writer.direction(event)
                if not self.localization_only:self.metadata[index]=event
                self.next_direction+=1;self.last_direction=digest
                return kind,event
            if self.audio is None:self.audio=_wire().SourceWindowAssembler(self.epoch,first_window=self.first_window)
            packet=self.audio.push(data)
            if packet is None:return None
            index=packet['window'];location=self.metadata.pop(index,None)
            if location is None:raise ValueError('完整音频缺少对应方向清单')
            expected={s['source_id'] for s in location['sources']}
            current={i for i,s in packet['tracks'].items() if s['current_confirmed']}
            if expected!=current:raise ValueError('音频与定位的当前源ID不一致')
            self.writer.audio(packet);self.last_audio_window=index
            if packet['final']:
                self.audio.finish()
                if self.metadata or self.next_direction!=index+1:raise ValueError('最终窗的方向区间不完整')
                self.closed=True;self.writer.close(completed=True)
            return kind,packet
        except Exception:
            self.failed=True;raise

    def finish(self,error=''):
        if not self.closed:return self.writer.close(completed=False,error=error if self.localization_only else error or '用户停止／缺少最终窗')
        return self.writer.result

class SourceNetworkWorker(QObject):
    state_changed=Signal(str)
    receive_error=Signal(str)
    lifecycle_event=Signal(int,str,str)
    fast_frame_ready=Signal(int)
    finished=Signal()
    def __init__(self,listen_ip,port,board_ip,output_root=None,*,idle_timeout_s=120.,evidence_source='UDP_RECEIVED_UNVERIFIED',raw_port=None):
        super().__init__();self.listen_ip=str(ipaddress.IPv4Address(listen_ip));self.board_ip=str(ipaddress.IPv4Address(board_ip))
        self.port=int(port)
        if not 1<=self.port<=65535 or self.port==5000:raise ValueError('逐源流使用独立端口，默认5001；不可使用旧5000端口')
        self.output_root=Path(output_root) if output_root is not None else None;self.idle_timeout=float(idle_timeout_s);self.evidence_source=evidence_source
        if not math.isfinite(self.idle_timeout) or not (self.idle_timeout==0 or 1<=self.idle_timeout<=120):raise ValueError('超时须为0（保持监听）或1到120秒')
        self.stop_event=threading.Event();self.lock=threading.Lock();self.latest={};self.result=None
        self.localization_only=False;self.raw_port=raw_port;self.raw_completed=0;self.raw_last=None
        self.expected_epoch=None;self.ignored_old_epoch=0
        self.display_boot=None;self.display_dropped=0;self.display_coverage={}
        self.bound_port=None;self.directory=None;self.receive_buffer_actual_bytes=None
        self.event_id=uuid.uuid4().int & 0x7fffffff

    def set_localization_only(self,enabled):
        with self.lock:self.localization_only=bool(enabled)
    def begin_localization_epoch(self,epoch):
        if not isinstance(epoch,int) or not 0<epoch<=0xffffffff:raise ValueError('定位epoch无效')
        with self.lock:
            if not self.localization_only:raise ValueError('只有仅定位模式允许切换接收epoch')
            if self.expected_epoch is not None and epoch<self.expected_epoch:raise ValueError('定位epoch不能回退')
            self.expected_epoch=epoch
    def begin_display_epoch(self,boot,epoch):
        if not isinstance(boot,int) or not 0<boot<=0xffffffff:raise ValueError('定位启动身份无效')
        with self.lock:
            if self.display_boot==boot and self.expected_epoch is not None and epoch<self.expected_epoch:return
            self.display_boot=boot
        self.begin_localization_epoch(epoch)
    def stop(self):self.stop_event.set()
    def take_snapshot(self):
        with self.lock:latest=self.latest;self.latest={}
        return latest

    def _publish(self,kind,event):
        if kind in ('directions','fast_directions'):preview=event
        else:
            preview=dict(window=event['window'],end=event['output_end_sample_exclusive'],final=event['final'],
                tracks={i:dict(current_confirmed=s['current_confirmed'],**{k:np.asarray(s[k][-2048:]).copy() for k in ['WDAS','enhanced']})
                    for i,s in event['tracks'].items()})
        with self.lock:
            pending=kind in self.latest
            self.latest[kind]=preview
        # 只给首次待取的新方向发排队通知；后续覆盖同一槽，不堆积GUI事件。
        if kind=='fast_directions' and not pending:self.fast_frame_ready.emit(self.event_id)

    @Slot()
    def run(self):
        sock=raw_sock=None;session=None;error='';raw_file=raw_index=None;raw_peer=None;raw_assembler=None
        active_epoch=None;active_display_boot=None;fast_peer=None;raw_error=""
        from mic16_av_fusion.fast_display_wire import DisplayResultStore,decode_direction
        display_store=DisplayResultStore()
        def close_raw():
            nonlocal raw_file,raw_index,raw_error
            if raw_assembler is not None:
                try:raw_assembler.finish()
                except ValueError as exc:raw_error=raw_error or str(exc)
            if raw_file is not None:raw_file.close();raw_index.close();raw_file=raw_index=None
            if self.directory is not None and raw_assembler is not None:
                _json(self.directory/'input_pcm_manifest.json',dict(dtype='float32_le',channels=16,rate_numerator=78125,rate_denominator=2,epoch=raw_assembler.epoch,completed_frames=raw_assembler.completed,joined_at_frame=raw_assembler.joined_at_frame,pending_frames=len(raw_assembler.pending),status="INCOMPLETE" if raw_error else "COMPLETE_FRAMES",error=raw_error,missing_data_filled=False,board_verified=False))
        def apply_epoch():
            nonlocal active_epoch,active_display_boot,session,raw_assembler,raw_peer,raw_error,fast_peer
            with self.lock:expected=self.expected_epoch;boot=self.display_boot
            if expected is None or (expected==active_epoch and boot==active_display_boot):return
            if session.epoch is not None or (raw_assembler is not None and raw_assembler.epoch is not None):
                close_raw();session.finish()
                if self.output_root is None:writer=NetworkPreview()
                else:
                    self.directory=self.output_root.resolve()/('source_udp_'+uuid.uuid4().hex)
                    writer=NetworkWaveWriter(self.directory,evidence_source=self.evidence_source)
                session=SourceNetworkSession(writer)
            session.set_localization_only(True)
            from .raw_pcm_wire import RawPcmAssembler
            raw_assembler=RawPcmAssembler();raw_peer=None;active_epoch=expected;raw_error=""
            active_display_boot=boot;fast_peer=None
            if boot is not None:display_store.reset(boot,expected)
            with self.lock:self.latest={};self.raw_last=None;self.display_coverage={}
        try:
            if self.output_root is None:
                writer=NetworkPreview()
            else:
                root=self.output_root.resolve();root.mkdir(parents=True,exist_ok=True)
                self.directory=root/('source_udp_'+uuid.uuid4().hex)
                writer=NetworkWaveWriter(self.directory,evidence_source=self.evidence_source)
            session=SourceNetworkSession(writer)
            sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4*1024*1024)
            self.receive_buffer_actual_bytes=sock.getsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF);sock.settimeout(.2)
            sock.bind((self.listen_ip,self.port));self.bound_port=sock.getsockname()[1]
            if self.raw_port is not None:
                from mic16_qt.services.raw_pcm_wire import RawPcmAssembler
                if not 1<=int(self.raw_port)<=65535 or int(self.raw_port)==self.port:raise ValueError('原始PCM端口无效')
                raw_assembler=RawPcmAssembler();raw_sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
                raw_sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4*1024*1024);raw_sock.bind((self.listen_ip,int(self.raw_port)))
            message=f'逐源监听 {self.listen_ip}:{self.bound_port} · '+(f'保存到 {self.directory}' if self.directory else '仅预览，未录制')
            self.state_changed.emit(message)
            self.lifecycle_event.emit(self.event_id,'ready',message)
            last=time.monotonic()
            while not self.stop_event.is_set() and not session.closed:
                with self.lock:session.set_localization_only(self.localization_only)
                apply_epoch()
                readable,_,_=select.select([sock]+([raw_sock] if raw_sock else []),[],[],.2)
                apply_epoch()
                if not readable:
                    if self.idle_timeout and time.monotonic()-last>=self.idle_timeout:raise TimeoutError('定位数据超时，会话未完成')
                    continue
                accepted=False
                for receiving in readable:
                    data,peer=receiving.recvfrom(65535)
                    if peer[0]!=self.board_ip:continue
                    if receiving is raw_sock:
                        if raw_error:continue
                        try:
                            if active_epoch is not None:
                                if len(data)<12 or data[:4] not in (b'M16L',b'M16P'):raise ValueError('仅定位epoch数据包无效')
                                packet_epoch=struct.unpack_from('<I',data,8)[0]
                                if packet_epoch<active_epoch:self.ignored_old_epoch+=1;continue
                                if packet_epoch>active_epoch:raise ValueError('收到尚未请求的定位epoch')
                            if raw_peer is not None and peer!=raw_peer:continue
                            frames=raw_assembler.push(data);raw_peer=peer;accepted=True
                            if session.epoch is not None and raw_assembler.epoch!=session.epoch:raise ValueError('原始PCM与定位epoch不一致')
                            for frame in frames:
                                self.raw_completed+=1;self.raw_last=frame
                                if self.directory is not None:
                                    if raw_file is None:
                                        raw_file=(self.directory/'input_16ch.f32').open('xb');raw_index=(self.directory/'input_pcm.jsonl').open('x',encoding='utf-8')
                                    raw_file.write(frame.pcm.tobytes());raw_index.write(json.dumps(dict(epoch=frame.epoch,frame=frame.frame,first_sample=frame.first_sample,samples=2048,channels=16,dtype='float32_le',scale_exponent=frame.scale_exponent,rate_numerator=78125,rate_denominator=2))+'\n')
                                with self.lock:self.latest['raw_pcm']=dict(epoch=frame.epoch,frame=frame.frame,first_sample=frame.first_sample,pcm=frame.pcm,completed=self.raw_completed)
                            continue
                        except ValueError as exc:
                            raw_error=str(exc)
                            with self.lock:self.latest['raw_error']=raw_error
                            self.state_changed.emit('原始PCM不完整，定位接收继续：'+raw_error)
                        continue
                    if active_display_boot is None and data[:4]==b'M16D':
                        # 已运行板卡可能先于HELLO响应发数据；握手前忽略，不结束接收线程。
                        self.ignored_old_epoch+=1;continue
                    if active_display_boot is not None:
                        try:
                            if fast_peer is not None and peer!=fast_peer:continue
                            if data[:4]==b'M16D':
                                frame=decode_direction(data)
                                if frame.epoch_key!=(active_display_boot,active_epoch):
                                    self.ignored_old_epoch+=1;continue
                                fast_peer=peer
                                if display_store.accept_current(frame):
                                    coverage=dict(boot=frame.boot,epoch=frame.epoch,sequence=frame.sequence,wire_version=frame.wire_version,
                                        first_sample=frame.first_sample,end_sample=frame.end_sample,
                                        uncovered_sample_interval=display_store.last_analysis_gap,
                                        uncovered_samples_total=display_store.analysis_skipped_samples,
                                        lost_result_packets=display_store.gaps,
                                        completed_tick=frame.completed_tick,tick_hz=frame.tick_hz,
                                        first_received_tick=frame.first_received_tick,last_front_tick=frame.last_front_tick,
                                        end_to_end_latency_verified=False)
                                    self.display_coverage=coverage
                                    if self.directory is not None:
                                        with (self.directory/'localization_windows.jsonl').open('a',encoding='utf-8') as index:
                                            index.write(json.dumps(coverage,ensure_ascii=False)+'\n')
                                    with self.lock:self.latest['display_coverage']=coverage
                                    self._publish('fast_directions',frame);accepted=True
                                else:self.display_dropped+=1
                            elif data[:4]==b'M16L':
                                event=directions(data)
                                if event['epoch']!=active_epoch:self.ignored_old_epoch+=1;continue
                                # 旧M16L没有boot字段，历史仅供浏览，不能据此自动关联增强输入。
                                event['session_binding_verified']=False
                                event['protocol']='M16L_LEGACY_NO_BOOT'
                                if display_store.accept_history(event,active_display_boot):
                                    session.writer.direction(event)
                                    with self.lock:self.latest['refined_history']=tuple(display_store.history.values())
                                accepted=True
                            else:raise ValueError('持续定位端口类型错误')
                        except ValueError as exc:
                            with self.lock:self.latest['display_error']=str(exc)
                        continue
                    if active_epoch is not None:
                        if len(data)<12 or data[:4] not in (b'M16L',b'M16P'):raise ValueError('仅定位epoch数据包无效')
                        packet_epoch=struct.unpack_from('<I',data,8)[0]
                        if packet_epoch<active_epoch:self.ignored_old_epoch+=1;continue
                        if packet_epoch>active_epoch:raise ValueError('收到尚未请求的定位epoch')
                    update=session.push(data,peer)
                    if raw_assembler is not None and raw_assembler.epoch is not None and session.epoch!=raw_assembler.epoch:raise ValueError('定位与原始PCM epoch不一致')
                    if session.peer==peer:accepted=True
                    if update is not None:self._publish(*update)
                # 其它端点的数据包不能续期本会话。
                if accepted:last=time.monotonic()
        except Exception as exc:
            error=str(exc);self.receive_error.emit(error)
            # 线程迅速失败时Qt可能先销毁发送者；不可依赖GUI槽中的sender()识别错误来源。
            self.lifecycle_event.emit(self.event_id,'error',error)
        finally:
            if sock is not None:sock.close()
            if raw_sock is not None:raw_sock.close()
            try:close_raw()
            except Exception as exc:
                error=error or str(exc)
                if raw_file is not None:raw_file.close();raw_index.close()
                if self.directory is not None:
                    _json(self.directory/'input_pcm_manifest.json',dict(status='INCOMPLETE',error=error,completed_frames=raw_assembler.completed,pending_frames=len(raw_assembler.pending),missing_data_filled=False,board_verified=False))
            if session is not None:
                try:
                    if self.output_root is None and self.stop_event.is_set() and not error and not session.closed:
                        self.result=session.writer.close(completed=False)
                    else:self.result=session.finish(error)
                except Exception as exc:
                    error=error or str(exc);self.result=dict(status='INCOMPLETE',error=error)
                self.result.update(directory=str(self.directory) if self.directory else None,recording_enabled=self.output_root is not None,UDP_packets=session.packets,
                    max_receiver_PCM_bytes=session.audio.max_pcm_bytes if session.audio else 0,
                    ignored_before_preview_join=session.ignored_before_join,ignored_endpoints=session.ignored_endpoints,receive_buffer_actual_bytes=self.receive_buffer_actual_bytes,
                    ignored_old_epoch=self.ignored_old_epoch,raw_error=raw_error,raw_completed_frames=self.raw_completed,display_coverage=self.display_coverage,board_verified=False)
            self.finished.emit()
