"""M16P：与定位输入逐位一致的16路F32 PCM；不进行增强、不伪造缺失样本。"""
from dataclasses import dataclass
from collections import OrderedDict
import struct
import zlib
import hashlib
import numpy as np

HEADER=struct.Struct('<4sHHIIQIIHHIHHIIIII')
PORT=5005
@dataclass(frozen=True)
class RawFrame:
    epoch: int
    frame: int
    first_sample: int
    scale_exponent: int
    pcm: np.ndarray
    sample_rate_numerator: int=78125
    sample_rate_denominator: int=2
    headroom_bits: int=0

def decode(data,*,with_headroom=False):
    if not isinstance(data,bytes) or len(data)!=1088:raise ValueError('M16P包长度错误')
    m,v,h,e,f,first,num,den,ch,fmt,total,part,count,offset,n,scale,flags,checksum=HEADER.unpack_from(data)
    if (m,v,h,num,den,ch,fmt,total,count,n)!=(b'M16P',1,64,78125,2,16,1,2048,128,16) or flags not in (0,2):raise ValueError('M16P格式或幅度合同错误')
    # 标志0保留原±1合同；标志2仅用于2^18标称增益及两位滤波峰值余量，不重缩放音频。
    if flags==2 and scale!=18:raise ValueError('M16P滤波余量与标称增益不匹配')
    if not e or first!=f*2048 or not 0<=part<128 or offset!=part*16 or scale>30:raise ValueError('M16P样本轴或分片序号错误')
    if zlib.crc32(data[:60]+data[64:])!=checksum:raise ValueError('M16P CRC错误')
    values=np.frombuffer(data,dtype='<f4',offset=64).reshape(16,16)
    if not np.isfinite(values).all() or np.any(np.abs(values)>(1<<flags)):raise ValueError('M16P PCM数值错误')
    result=(e,f,first,scale,part,values)
    return (*result,flags) if with_headroom else result

class RawPcmAssembler:
    """有限乱序缓存；重复片须完全相同，超过窗口报错，禁止补零掩盖丢包。"""
    def __init__(self,*,maximum_pending=4,history_frames=40):
        if not 1<=maximum_pending<=8 or not 20<=history_frames<=80:raise ValueError('PCM缓存配置错误')
        self.maximum_pending=maximum_pending;self.history_frames=history_frames
        self.epoch=self.next_frame=self.joined_at_frame=None;self.pending={};self.history=OrderedDict();self.completed=0;self.duplicates=0;self.ignored_before_join=0
        self.fingerprints=OrderedDict()
    def push(self,data):
        e,f,first,scale,part,values,headroom=decode(data,with_headroom=True);digest=hashlib.sha256(data).digest()
        if self.epoch is None:self.epoch=e;self.next_frame=self.joined_at_frame=f
        if e!=self.epoch:raise ValueError('PCM epoch改变，请重新开始会话')
        if f<self.joined_at_frame:self.ignored_before_join+=1;return []
        if f<self.next_frame:
            previous=self.fingerprints.get(f)
            if previous is None or previous[part]!=digest:raise ValueError('已提交PCM帧收到冲突或过旧分片')
            self.duplicates+=1;return []
        if f>=self.next_frame+self.maximum_pending:raise ValueError('PCM缺帧或乱序超过有界缓存')
        if f not in self.pending:self.pending[f]=[np.empty((2048,16),'<f4'),[None]*128,scale,0,headroom]
        state=self.pending[f]
        if state[2]!=scale:raise ValueError('同帧PCM归一化参数冲突')
        if state[4]!=headroom:raise ValueError('同帧PCM幅度合同冲突')
        if state[1][part] is not None:
            if state[1][part]!=digest:raise ValueError('PCM重复分片内容冲突')
            self.duplicates+=1;return []
        state[0][part*16:part*16+16]=values;state[1][part]=digest;state[3]+=1
        ready=[]
        while self.next_frame in self.pending and self.pending[self.next_frame][3]==128:
            i=self.next_frame;a,hashes,k,_,headroom=self.pending.pop(i);a.setflags(write=False)
            frame=RawFrame(e,i,i*2048,k,a,headroom_bits=headroom);ready.append(frame);self.history[i]=frame;self.fingerprints[i]=hashes
            self.completed+=1;self.next_frame+=1
            while len(self.history)>self.history_frames:self.history.popitem(last=False)
            while len(self.fingerprints)>self.maximum_pending:self.fingerprints.popitem(last=False)
        return ready
    def context(self,epoch,window):
        if epoch!=self.epoch:return None
        frames=[self.history.get(window*20+i) for i in range(20)]
        if any(f is None for f in frames):return None
        return np.concatenate([f.pcm for f in frames],axis=0)
    @property
    def buffered_pcm_bytes(self):return sum(x[0].nbytes for x in self.pending.values())+sum(x.pcm.nbytes for x in self.history.values())
    def finish(self):
        if self.pending:raise ValueError('PCM会话结束时仍有未完成分片，未补零')
