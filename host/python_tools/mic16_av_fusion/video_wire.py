"""M16V v1有界组帧；相机采集、物理时间核验和GUI均由调用方提供。"""
from __future__ import annotations
from dataclasses import dataclass
import math
import struct
import time
import zlib

HEADER=struct.Struct('<4sHHIIQHHHHIIIIIIHHHHII')
PAYLOAD=1000
MAX_BYTES=800*480*3
TIME_VERIFIED=1

@dataclass(frozen=True)
class VideoMeta:
    epoch:int
    frame_id:int
    capture_us:int
    width:int
    height:int
    format:int
    flags:int
    mount_id:int
    mode_id:int
    clock_domain:int
    total_bytes:int
    frame_crc32:int
    transform_id:int

@dataclass(frozen=True)
class VideoFrame:
    meta:VideoMeta
    pixels:bytes
    received_monotonic_s:float

def decode_packet(packet:bytes):
    """先检查固定合同和包CRC；拒绝不确定偏移、长度、格式和无时钟域的已核验时间。"""
    if len(packet)<HEADER.size or len(packet)>HEADER.size+PAYLOAD:
        raise ValueError('视频包长度越界')
    (magic,version,header,epoch,frame,capture,width,height,fmt,flags,mount,mode,domain,total,offset,
     frame_crc,index,chunks,size,reserved,transform,packet_crc)=HEADER.unpack_from(packet)
    if (magic!=b'M16V' or version!=1 or header!=72 or reserved or not epoch or not mount or not mode or not transform or
        not 1<=width<=800 or not 1<=height<=480 or fmt not in (1,2) or flags not in (0,1) or flags and not domain):
        raise ValueError('视频包版本或元数据无效')
    expected=width*height*(2 if fmt==1 else 3)
    if (total!=expected or total>MAX_BYTES or chunks!=(total+PAYLOAD-1)//PAYLOAD or
        index>=chunks or offset!=index*PAYLOAD or size!=min(PAYLOAD,total-offset) or len(packet)!=72+size):
        raise ValueError('视频分片范围无效')
    if zlib.crc32(packet[72:],zlib.crc32(packet[:68]))&0xffffffff!=packet_crc:
        raise ValueError('视频包CRC错误')
    meta=VideoMeta(epoch,frame,capture,width,height,fmt,flags,mount,mode,domain,total,frame_crc,transform)
    return meta,index,chunks,offset,packet[72:]

def _newer(a,b):
    return 0<((a-b)&0xffffffff)<0x80000000

class VideoAssembler:
    """最多保留两个相邻帧，过期/不完整帧丢弃；epoch变化须显式重启会话。"""
    def __init__(self,*,max_frames=2,timeout_s=.5):
        if max_frames not in (1,2) or not math.isfinite(timeout_s) or not .01<=timeout_s<=2:
            raise ValueError('视频组帧配置越界')
        self.max_frames=max_frames;self.timeout_s=timeout_s
        self.peer=None;self.epoch=None;self.newest=None;self.published=None
        self.pending={};self.blocked=set();self.last_now=None
        self.stats=dict(packets=0,completed=0,duplicates=0,stale=0,expired=0,evicted=0,rejected=0)

    def push(self,packet,peer,*,now_s=None):
        now=time.monotonic() if now_s is None else float(now_s)
        if not math.isfinite(now) or self.last_now is not None and now<self.last_now:
            raise ValueError('主机单调时钟倒退')
        self.last_now=now
        try:
            meta,index,chunks,offset,payload=decode_packet(bytes(packet))
        except ValueError:
            self.stats['rejected']+=1;raise
        if self.peer is None:self.peer=peer;self.epoch=meta.epoch
        if peer!=self.peer or meta.epoch!=self.epoch:
            self.stats['rejected']+=1;raise ValueError('视频来源或epoch改变，需要新会话')
        self.stats['packets']+=1
        for identity,entry in list(self.pending.items()):
            if now-entry[0]>self.timeout_s:
                del self.pending[identity];self.blocked.add(identity);self.stats['expired']+=1
        if self.newest is None or _newer(meta.frame_id,self.newest):
            self.newest=meta.frame_id
        distance=(self.newest-meta.frame_id)&0xffffffff
        if distance>=self.max_frames or self.published is not None and not _newer(meta.frame_id,self.published) or meta.frame_id in self.blocked:
            self.stats['stale']+=1;return None
        self.blocked={n for n in self.blocked if ((self.newest-n)&0xffffffff)<self.max_frames}
        for identity in list(self.pending):
            if ((self.newest-identity)&0xffffffff)>=self.max_frames:
                del self.pending[identity];self.stats['evicted']+=1
        if meta.frame_id not in self.pending:
            self.pending[meta.frame_id]=[now,meta,bytearray(meta.total_bytes),bytearray(chunks),0]
        entry=self.pending[meta.frame_id]
        if entry[1]!=meta:
            del self.pending[meta.frame_id];self.blocked.add(meta.frame_id);self.stats['rejected']+=1
            raise ValueError('同帧元数据不一致')
        if entry[3][index]:
            if entry[2][offset:offset+len(payload)]!=payload:
                del self.pending[meta.frame_id];self.blocked.add(meta.frame_id);self.stats['rejected']+=1
                raise ValueError('重复分片内容冲突')
            self.stats['duplicates']+=1;return None
        entry[2][offset:offset+len(payload)]=payload;entry[3][index]=1;entry[4]+=1
        if entry[4]!=chunks:return None
        del self.pending[meta.frame_id]
        pixels=bytes(entry[2])
        if zlib.crc32(pixels)&0xffffffff!=meta.frame_crc32:
            self.blocked.add(meta.frame_id);self.stats['rejected']+=1;raise ValueError('完整视频帧CRC错误')
        self.published=meta.frame_id;self.stats['completed']+=1
        for identity in list(self.pending):
            if not _newer(identity,self.published):del self.pending[identity];self.stats['evicted']+=1
        return VideoFrame(meta,pixels,now)

    @property
    def pending_payload_bytes(self):
        return sum(len(entry[2]) for entry in self.pending.values())

def to_rgb888(frame:VideoFrame):
    """按元数据解码；只返回RGB像素，不冒充摄像头来源或时间核验。"""
    import numpy as np
    m=frame.meta
    if m.format==2:return np.frombuffer(frame.pixels,dtype=np.uint8).reshape(m.height,m.width,3).copy()
    raw=np.frombuffer(frame.pixels,dtype='<u2').reshape(m.height,m.width)
    r=(raw>>11).astype(np.uint8);g=((raw>>5)&63).astype(np.uint8);b=(raw&31).astype(np.uint8)
    return np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)),axis=-1)
