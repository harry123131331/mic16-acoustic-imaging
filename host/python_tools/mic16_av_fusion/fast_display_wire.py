"""持续方向与真实响应v2/v3；会话/样本区间是身份，接收时刻只用于断流检查。"""
from __future__ import annotations
from collections import OrderedDict
from dataclasses import dataclass
import math
import struct
import time
import zlib
import numpy as np

HEADER=struct.Struct('<4sHHIIQQQQQQQIIIIHHI')
assert HEADER.size==96
ALGORITHM=0xa204
from .hemisphere_grid import HEMISPHERE_ALGORITHM,raw_grid_direction

@dataclass(frozen=True)
class FastDisplayFrame:
    boot:int
    epoch:int
    sequence:int
    first_sample:int
    end_sample:int
    completed_tick:int
    tick_hz:int
    first_received_tick:int
    last_front_tick:int
    valid:bool
    map_valid:bool
    reason:int
    grid:int
    azimuth_deg:float
    elevation_deg:float
    coherence:float
    contrast:float
    support_fraction:float
    values:np.ndarray|None
    received_monotonic_s:float
    algorithm:int=ALGORITHM
    wire_version:int=2

    @property
    def key(self):return self.boot,self.epoch,self.sequence,self.first_sample,self.end_sample
    @property
    def epoch_key(self):return self.boot,self.epoch
    @property
    def window(self):return self.sequence

@dataclass(frozen=True)
class FastResponseFrame:
    boot:int
    epoch:int
    sequence:int
    first_sample:int
    end_sample:int
    completed_tick:int
    tick_hz:int
    first_received_tick:int
    last_front_tick:int
    valid:bool
    map_valid:bool
    reason:int
    grid:int
    values:np.ndarray
    received_monotonic_s:float
    algorithm:int=ALGORITHM
    wire_version:int=2
    @property
    def key(self):return self.boot,self.epoch,self.sequence,self.first_sample,self.end_sample
    @property
    def window(self):return self.sequence

def same_observation(direction,response):
    return (direction is not None and response is not None and direction.key==response.key and
        all(getattr(direction,k)==getattr(response,k) for k in ('completed_tick','tick_hz','first_received_tick','last_front_tick','valid','map_valid','reason','grid','algorithm','wire_version')))

def decode(data):
    if not isinstance(data,bytes) or len(data)<96:raise ValueError('定位响应报文过短')
    fields=HEADER.unpack_from(data)
    magic,version,size,boot,epoch,seq,first,end,done,hz,received,front,algorithm,flags,reason,grid,offset,count,checksum=fields
    if magic not in (b'M16D',b'M16H') or size!=96 or (version,algorithm) not in ((2,ALGORITHM),(3,ALGORITHM),(4,HEMISPHERE_ALGORITHM)):raise ValueError('定位响应版本错误')
    if not boot or not epoch or first%2048 or end!=first+8192 or end>0xffffffffffffffff or (version==2 and first!=seq*8192):raise ValueError('定位响应采样区间错误')
    if not hz or not received<=front<=done or flags not in (0,2,3) or not 0<=reason<=2 or bool(flags&1)!=(reason==0) or not 0<=grid<325:raise ValueError('定位响应状态错误')
    if (magic==b'M16D' and (offset,count)!=(0,5)) or (magic==b'M16H' and ((offset,count) not in ((0,240),(240,85)) or not flags&2)):raise ValueError('定位响应分片错误')
    n=count*(8 if magic==b'M16D' else 4)
    if len(data)!=96+n or zlib.crc32(data[96:],zlib.crc32(data[:92]))!=checksum:raise ValueError('定位响应长度或CRC错误')
    values=np.frombuffer(data,dtype='<f8' if magic==b'M16D' else '<f4',offset=96)
    if not np.isfinite(values).all():raise ValueError('定位响应不是有限值')
    if magic==b'M16D':
        az,el,coherence,contrast,support=values
        limit=90 if version==4 else 60
        el_limit=90 if version==4 else 30
        if not(-limit<=az<=limit and -el_limit<=el<=el_limit and -2.000005<=coherence<=2.000005 and 0<=contrast<=4.00001 and 0<=support<=1):raise ValueError('方向或质量越界')
        expected=raw_grid_direction(grid) if version==4 else (-60+5*(grid%25),-30+5*(grid//25))
        if flags&2 and (abs(az-expected[0])>1e-10 or abs(el-expected[1])>1e-10):raise ValueError('网格方向不符')
    elif np.max(np.abs(values))>2.000005:raise ValueError('空间响应越界')
    return fields,data[96:]

def decode_direction(data,now=None):
    f,payload=decode(data)
    magic,_,_,boot,epoch,seq,first,end,done,hz,received,front,algorithm,flags,reason,grid,offset,count,_=f
    if magic!=b'M16D':raise ValueError('不是方向报文')
    az,el,coherence,contrast,support=struct.unpack('<5d',payload)
    return FastDisplayFrame(boot,epoch,seq,first,end,done,hz,received,front,bool(flags&1),bool(flags&2),reason,grid,az,el,coherence,contrast,support,None,time.monotonic() if now is None else float(now),algorithm=algorithm,wire_version=f[1])

class FastDisplayAssembler:
    """只保留最新两个待组装样本组；收到HELLO/START身份后再接收新epoch。"""
    def __init__(self,*,response_only=False):
        self.response_only=bool(response_only)
        self.pending=OrderedDict();self.binding=None;self.peer=None;self.last=-1;self.newest=-1;self.blocked=set()
        self.stats=dict(completed=0,duplicates=0,obsolete=0,discarded=0,rejected=0,gaps=0)
    def reset(self,boot,epoch):
        if not (isinstance(boot,int) and 0<boot<=0xffffffff and isinstance(epoch,int) and 0<epoch<=0xffffffff):raise ValueError('定位会话身份无效')
        if self.binding is not None and self.binding[0]==boot and epoch<self.binding[1]:raise ValueError('定位epoch回退')
        self.binding=(boot,epoch);self.peer=None;self.pending.clear();self.last=self.newest=-1;self.blocked.clear()
    def expire(self,now=None):
        now=time.monotonic() if now is None else float(now)
        for seq in list(self.pending):
            if now-self.pending[seq]['time']>1.:
                del self.pending[seq];self.blocked.add(seq);self.stats['discarded']+=1
        self.blocked={s for s in self.blocked if s>=self.newest-1}
    @property
    def pending_payload_bytes(self):return sum(sum(len(p) for p in x['parts'].values()) for x in self.pending.values())
    def push(self,data,peer,now=None):
        now=time.monotonic() if now is None else float(now);self.expire(now)
        try:
            f,payload=decode(data)
            magic,_,_,boot,epoch,seq,first,end,done,hz,received,front,algorithm,flags,reason,grid,offset,count,_=f
            if self.response_only and magic!=b'M16H':raise ValueError('空间图接收器不接受方向报文')
            if self.binding is None or self.binding!=(boot,epoch):raise ValueError('定位会话未确认或不匹配')
            if self.peer is not None and peer!=self.peer:raise ValueError('定位发送端改变')
            if seq<=self.last or seq<self.newest-1 or seq in self.blocked:self.stats['obsolete']+=1;return None
            self.newest=max(self.newest,seq);self.peer=peer
            # 两类报文的类型/片号不同，共同身份是同一头的boot到grid。
            identity=data[4:6]+data[8:88];part=-1 if magic==b'M16D' else offset
            if seq not in self.pending:
                if len(self.pending)>=2:
                    oldest=min(self.pending)
                    if seq<oldest:self.stats['obsolete']+=1;return None
                    del self.pending[oldest];self.stats['discarded']+=1
                self.pending[seq]=dict(identity=identity,parts={},time=now)
            slot=self.pending[seq]
            if slot['identity']!=identity:
                del self.pending[seq];self.blocked.add(seq);raise ValueError('方向与空间图身份冲突')
            if part in slot['parts']:
                if slot['parts'][part]!=payload:
                    del self.pending[seq];self.blocked.add(seq);raise ValueError('定位重复片冲突')
                self.stats['duplicates']+=1;return None
            slot['parts'][part]=payload
            if (not self.response_only and -1 not in slot['parts']) or (flags&2 and (0 not in slot['parts'] or 240 not in slot['parts'])):return None
            values=None
            if flags&2:
                values=np.frombuffer(slot['parts'][0]+slot['parts'][240],dtype='<f4').copy().reshape(13,25);values.setflags(write=False)
            if self.response_only:
                result=FastResponseFrame(boot,epoch,seq,first,end,done,hz,received,front,bool(flags&1),bool(flags&2),reason,grid,values,now,algorithm=algorithm,wire_version=f[1])
            else:
                az,el,coherence,contrast,support=struct.unpack('<5d',slot['parts'][-1])
                result=FastDisplayFrame(boot,epoch,seq,first,end,done,hz,received,front,bool(flags&1),bool(flags&2),reason,grid,az,el,coherence,contrast,support,values,now,algorithm=algorithm,wire_version=f[1])
            if self.last>=0 and seq>self.last+1:self.stats['gaps']+=seq-self.last-1
            self.last=seq;self.stats['completed']+=1
            for key in list(self.pending):
                if key<=seq:del self.pending[key]
            return result
        except (ValueError,struct.error):self.stats['rejected']+=1;raise

class DisplayResultStore:
    """当前方向只接收新采样结果；C16记录进入独立有界历史。"""
    def __init__(self,history_limit=128):
        if not isinstance(history_limit,int) or not 1<=history_limit<=512:raise ValueError('历史上限无效')
        self.history_limit=history_limit;self.current=None;self.history=OrderedDict();self.evicted=0
        self.binding=None;self.obsolete=0;self.gaps=0;self.analysis_skipped_samples=0;self.last_analysis_gap=(0,0)
    def reset(self,boot,epoch):
        self.binding=(boot,epoch);self.current=None;self.history.clear()
        self.analysis_skipped_samples=0;self.last_analysis_gap=(0,0)
    def accept_current(self,frame):
        if self.binding!=frame.epoch_key:raise ValueError('当前方向会话身份错误')
        if self.current is not None:
            if (frame.wire_version,frame.algorithm)!=(self.current.wire_version,self.current.algorithm):raise ValueError('同一定位epoch内协议或网格改变')
            if frame.sequence<=self.current.sequence or frame.end_sample<=self.current.end_sample:self.obsolete+=1;return False
            if frame.wire_version>=3 and frame.first_sample<self.current.end_sample:raise ValueError('最新窗口重复使用已分析采样')
            self.gaps+=max(0,frame.sequence-self.current.sequence-1)
        previous_end=self.current.end_sample if self.current is not None else 0
        self.last_analysis_gap=(previous_end,frame.first_sample)
        # 这里只统计定位结果覆盖缺口；RAW完整性由独立PCM接收器核验。
        # 网络漏结果也会扩大这个缺口，不能把它全称为板端主动跳过。
        self.analysis_skipped_samples+=max(0,frame.first_sample-previous_end)
        self.current=frame;return True
    def accept_history(self,event,boot):
        if self.binding!=(boot,event['epoch']):raise ValueError('历史结果会话身份错误')
        key=(boot,event['epoch'],event['input_start_sample'],event['input_end_sample_exclusive'])
        if key in self.history:
            if self.history[key]!=event:raise ValueError('历史同区间结果冲突')
            return False
        self.history[key]=event
        if len(self.history)>self.history_limit:self.history.popitem(last=False);self.evicted+=1
        return True
    def stale(self,now=None):
        now=time.monotonic() if now is None else float(now)
        return self.current is None or not 0<=now-self.current.received_monotonic_s<1.
