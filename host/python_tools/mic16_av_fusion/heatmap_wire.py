"""M16H空间响应：完整两片校验后才显示，固定量纲，不从方向点造热图。"""
from __future__ import annotations
from dataclasses import dataclass
import struct
import time
import zlib
import numpy as np

HEADER=struct.Struct('<4sHHIIQQHHHHffffIIII')
assert HEADER.size==72

@dataclass(frozen=True)
class HeatmapFrame:
    epoch:int
    window:int
    values:np.ndarray
    received_monotonic_s:float
    response_kind:str='mean_PHAT_response_480_maps_not_SPL_or_C16_score'

def decode(data):
    if not isinstance(data,bytes) or len(data)<72:raise ValueError('热图报文过短')
    magic,version,size,epoch,window,begin,end,rows,cols,kind,flags,az,el,daz,del_,aggregation,offset,count,checksum=HEADER.unpack_from(data)
    if (magic,version,size,rows,cols,kind,flags,az,el,daz,del_,aggregation)!=(b'M16H',1,72,13,25,1,1,-60.,-30.,5.,5.,1):raise ValueError('热图版本、网格或响应类型错误')
    if not epoch or (begin,end)!=(window*40960,(window+1)*40960):raise ValueError('热图时段错误')
    if (offset,count) not in ((0,240),(240,85)) or len(data)!=72+count*4:raise ValueError('热图分片区间错误')
    if zlib.crc32(data[72:],zlib.crc32(data[:68]))!=checksum:raise ValueError('热图CRC错误')
    values=np.frombuffer(data,dtype='<f4',offset=72)
    if not np.isfinite(values).all() or np.max(np.abs(values))>2:raise ValueError('热图数值错误')
    return epoch,window,offset,data[:60],data[72:]

class HeatmapAssembler:
    def __init__(self):
        self.pending={};self.peer=None;self.epoch=None;self.last=-1;self.newest=-1;self.blocked=set()
        self.stats=dict(completed=0,rejected=0,duplicates=0,obsolete=0,discarded=0)

    def expire(self,now=None):
        now=time.monotonic() if now is None else now
        for key in list(self.pending):
            if now-self.pending[key]['time']>1.:
                del self.pending[key];self.blocked.add(key);self.stats['discarded']+=1

    @property
    def pending_payload_bytes(self):return sum(sum(len(b) for b in p['parts'].values()) for p in self.pending.values())

    def push(self,data,peer,now=None):
        now=time.monotonic() if now is None else now;self.expire(now)
        try:
            epoch,window,offset,identity,payload=decode(data)
            if self.peer is not None and (peer!=self.peer or epoch!=self.epoch):raise ValueError('热图发送端或epoch改变，请重新监听')
            if window<=self.last or window<self.newest-1 or window in self.blocked:self.stats['obsolete']+=1;return None
            self.newest=max(self.newest,window);self.blocked={w for w in self.blocked if w>=self.newest-1}
            if self.peer is None:self.peer,self.epoch=peer,epoch
            if window not in self.pending:
                if len(self.pending)>=2:
                    # 仅保留最新两个窗，不为迟到的旧窗挤掉较新数据。
                    if window<min(self.pending):self.stats['obsolete']+=1;return None
                    del self.pending[min(self.pending)];self.stats['discarded']+=1
                self.pending[window]=dict(identity=identity,parts={},time=now)
            pending=self.pending[window]
            if identity!=pending['identity']:del self.pending[window];self.blocked.add(window);raise ValueError('热图同窗元数据冲突')
            if offset in pending['parts']:
                if pending['parts'][offset]!=payload:del self.pending[window];self.blocked.add(window);raise ValueError('热图重复片冲突')
                self.stats['duplicates']+=1;return None
            pending['parts'][offset]=payload
            if len(pending['parts'])<2:return None
            values=np.frombuffer(pending['parts'][0]+pending['parts'][240],dtype='<f4').copy().reshape(13,25);values.setflags(write=False)
            self.last=window;self.stats['completed']+=1
            for key in list(self.pending):
                if key<=window:del self.pending[key]
            return HeatmapFrame(epoch,window,values,now)
        except (ValueError,struct.error):self.stats['rejected']+=1;raise
