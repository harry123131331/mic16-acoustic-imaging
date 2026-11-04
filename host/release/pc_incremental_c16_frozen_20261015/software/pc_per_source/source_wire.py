"""Strict offline M16S receiver contract; not connected to Qt or board UDP yet.

One bounded window is assembled atomically across all anonymous source IDs.
Invalid/gapped/conflicting epochs fail closed. Never pad missing audio.
"""
from dataclasses import dataclass
import hashlib
import struct
import zlib
import numpy as np

HEADER=struct.Struct('<4sHHIIIHHQQIIIHHI')
CHUNK=128
MAX_BYTES=1084

@dataclass(frozen=True)
class SourcePacket:
    epoch:int; window:int; source_id:int; ordinal:int; streams:int
    start:int; end:int; offset:int; total:int; count:int
    current:bool; final:bool; empty:bool; pcm:np.ndarray

def decode(datagram):
    if not isinstance(datagram,bytes) or not 60<=len(datagram)<=MAX_BYTES:
        raise ValueError('M16S datagram length/type')
    (magic,version,flags,epoch,window,identity,ordinal,streams,start,end,offset,total,
     rate,count,reserved,checksum)=HEADER.unpack_from(datagram)
    if magic!=b'M16S' or version!=1 or flags&~7 or reserved or not epoch or rate!=16000:
        raise ValueError('M16S schema/epoch/rate')
    expected_start=(window*40960*256+624)//625
    expected_end=((window+1)*40960*256+624)//625
    if (start,end,total)!=(expected_start,expected_end,expected_end-expected_start) or streams>24:
        raise ValueError('M16S global sample interval/capacity')
    if flags&4:
        if identity or streams or ordinal or count or offset or flags&1:
            raise ValueError('M16S empty-window manifest')
    elif not identity or not 1<=streams<=24 or ordinal>=streams or offset%CHUNK or not 0<=offset<total or count!=min(CHUNK,total-offset):
        raise ValueError('M16S source/chunk bounds')
    if len(datagram)!=60+count*8 or zlib.crc32(datagram[:56]+datagram[60:])!=checksum:
        raise ValueError('M16S payload length/CRC32')
    pcm=np.frombuffer(datagram,dtype='<f4',offset=60).reshape(count,2)
    if not np.isfinite(pcm).all():raise ValueError('M16S nonfinite PCM')
    return SourcePacket(epoch,window,identity,ordinal,streams,start,end,offset,total,count,
        bool(flags&1),bool(flags&2),bool(flags&4),pcm)

class SourceWindowAssembler:
    """Caller consumes returned windows synchronously; no unbounded queue.

    Max PCM allocation: 24*16778*2*4 = 3,221,376 bytes, plus bounded
    chunk receipts. At most 8 current sources, remaining IDs are audio halos.
    first_window must be explicit if attaching after the beginning of an epoch.
    """
    def __init__(self,epoch,first_window=0):
        if type(epoch)is not int or not 1<=epoch<=0xffffffff or type(first_window)is not int or not 0<=first_window<=0xffffffff:
            raise ValueError('Explicit nonzero uint32 epoch/window required')
        self.epoch=epoch;self.first_window=first_window;self.expected=first_window;self.failed=False;self.closed=False
        self.slots={};self.meta=None;self.last_receipts={};self.receipts={};self.max_pcm_bytes=0

    def push(self,datagram):
        if self.failed:raise RuntimeError('Failed source epoch; create a NEW epoch')
        try:return self._push(datagram)
        except Exception:
            self.failed=True;self.slots.clear();self.receipts.clear();raise

    def _push(self,datagram):
        p=decode(datagram);digest=hashlib.sha256(datagram).digest()
        key=(p.window,p.ordinal,p.offset)
        if p.epoch!=self.epoch:raise ValueError('Source epoch changed')
        if p.window==self.expected-1 and self.last_receipts.get(key)==digest:return None
        if self.closed or p.window!=self.expected:raise ValueError('Missing/conflicting source window; no gap filling')
        meta=(p.streams,p.start,p.end,p.final)
        if self.meta is not None and self.meta!=meta:raise ValueError('Conflicting window manifest')
        self.meta=meta
        if key in self.receipts:
            if self.receipts[key]!=digest:raise ValueError('Conflicting duplicate source packet')
            return None
        self.receipts[key]=digest
        if not p.empty:
            if p.ordinal not in self.slots:
                if any(s['source_id']==p.source_id for s in self.slots.values()):raise ValueError('Duplicate source ID/ordinal')
                self.slots[p.ordinal]=dict(source_id=p.source_id,current_confirmed=p.current,
                    pcm=np.empty((p.total,2),np.float32),chunks=set())
            s=self.slots[p.ordinal]
            if (s['source_id'],s['current_confirmed'])!=(p.source_id,p.current):raise ValueError('Source ID/status changed within window')
            s['pcm'][p.offset:p.offset+p.count]=p.pcm;s['chunks'].add(p.offset)
            self.max_pcm_bytes=max(self.max_pcm_bytes,sum(s['pcm'].nbytes for s in self.slots.values()))
        complete=p.empty or (len(self.slots)==p.streams and all(len(s['chunks'])==(p.total+127)//128 for s in self.slots.values()))
        if not complete:return None
        if sum(s['current_confirmed'] for s in self.slots.values())>8:raise ValueError('More than 8 current sources')
        tracks={s['source_id']:dict(WDAS=s['pcm'][:,0],enhanced=s['pcm'][:,1],
            current_confirmed=s['current_confirmed']) for _,s in sorted(self.slots.items())}
        result=dict(epoch=p.epoch,window=p.window,output_start_sample=p.start,output_end_sample_exclusive=p.end,
            output_rate=16000,final=p.final,tracks=tracks,transition_audio_is_not_current_detection=True,
            timestamp_domain='relative_audio_sample_clock_not_camera_calibrated',transport='M16S_OFFLINE_CONTRACT')
        self.expected+=1;self.closed=p.final;self.slots={};self.meta=None
        self.last_receipts=self.receipts;self.receipts={}
        return result

    def finish(self):
        if self.failed or self.slots or self.meta is not None or not self.closed:
            self.failed=True;raise ValueError('Incomplete/final-missing source segment; discard partial window')
        return dict(epoch=self.epoch,completed_windows=self.expected-self.first_window,max_pcm_bytes=self.max_pcm_bytes,
            transport_missing_audio_filled=False,board_UDP_verified=False)
