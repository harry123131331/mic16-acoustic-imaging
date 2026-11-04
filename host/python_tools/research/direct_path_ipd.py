"""Untrained, class-free direct-path phase interface for the fixed MIC16 array.

Paper-inspired prototype, not a reproduction of IPDnet/IPDnet2. No old model
or dataset is modified. Forward receives observed STFT only. Geometry, source
activity masks and true directions enter targets/verification, never forward.
"""
from itertools import permutations
import math
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION='mic16_native_direct_ipd_interface_untrained_v1_20261012'
FS,FRAME,TIME,CHANNELS,SPEED=39062.5,2048,20,16,343.
TRACKS,HIDDEN,POOL=3,16,4
MIN_ACTIVE=math.ceil(.020*FS)
FREQ_BINS=np.flatnonzero((np.fft.rfftfreq(FRAME,1/FS)>0)&(np.fft.rfftfreq(FRAME,1/FS)<=8000))
FREQUENCIES=np.fft.rfftfreq(FRAME,1/FS)[FREQ_BINS]
PAIRS=np.column_stack((np.zeros(15,dtype=int),np.arange(1,16)))

def local_microphones():
    # 数据集坐标：X水平、Y向前、Z竖直，通道顺序固定。
    a=np.arange(CHANNELS)*2*np.pi/CHANNELS
    return np.column_stack((.08*np.cos(a),np.zeros(CHANNELS),.08*np.sin(a)))

def unit_vectors(directions):
    d=np.asarray(directions,float).reshape(-1,2);a,e=np.deg2rad(d).T
    return np.column_stack((np.cos(e)*np.sin(a),np.cos(e)*np.cos(a),np.sin(e)))

def signature_from_delays(relative_seconds):
    delays=np.asarray(relative_seconds,float)
    assert delays.shape[-1]==15 and np.isfinite(delays).all()
    phase=-2*np.pi*FREQUENCIES[:,None]*delays[...,None,:]
    return np.stack((np.cos(phase),np.sin(phase)),axis=-1).astype(np.float32)

def direction_signatures(directions):
    u=unit_vectors(directions);m=local_microphones()
    delays=-(u@(m[1:]-m[0]).T)/SPEED
    return signature_from_delays(delays)

def exact_ray_signature(state,source):
    distance=np.linalg.norm(np.asarray(state['microphone_positions_m'])-source['position_m'],axis=1)
    return signature_from_delays((distance[1:]-distance[0])/SPEED)

class PhaseGeometry:
    def __init__(self):
        a,e=np.meshgrid(np.arange(-60,61,5),np.arange(-30,31,5))
        self.directions=np.column_stack((a.ravel(),e.ravel())).astype(np.float32)
        self.templates=direction_signatures(self.directions)
        # 在本工程二维搜索网格上取均值，不套用论文水平圆的J0公式。
        # 这是拒判目标，不是实测扩散噪声相干性或功率谱。
        self.no_source=self.templates.mean(0,dtype=np.float64).astype(np.float32)
        self.centered=self.templates-self.no_source
        self.norm=np.square(self.centered.astype(float)).sum((1,2,3))
        for a in (self.directions,self.templates,self.no_source,self.centered,self.norm):a.flags.writeable=False

def observations(frames):
    """Native-rate non-overlapping causal frames; no pure background reference."""
    x=np.asarray(frames,np.float64)
    if x.shape!=(TIME,FRAME,CHANNELS) or not np.isfinite(x).all():
        raise ValueError('observed finite PCM[20,2048,16] required')
    window=.5-.5*np.cos(2*np.pi*np.arange(FRAME)/FRAME)
    s=np.fft.rfft((x-x.mean(1,keepdims=True))*window[None,:,None],axis=1)[:,FREQ_BINS]
    scale=np.maximum(np.abs(s).mean((1,2),keepdims=True),1e-8)
    s=s/scale
    out=np.stack((s.real,s.imag),axis=-1).reshape(TIME,len(FREQ_BINS),32).astype(np.float32)
    assert np.isfinite(out).all()
    return np.ascontiguousarray(out)

def targets(record,activity_masks,geometry):
    """Keep original window >=782-sample truth; derive fractional frame occupancy.

    Weighted phase residual beta*(q-q0) borrows SRP-DNN's activity encoding,
    but uses separate tracks. A window with a genuine1..781-sample gray source
    has loss_weight0, avoiding converting uncertain sources to no-source labels.
    This conservative gray-window policy must be counted before future training.
    """
    details=record['source_details']
    assert len(details)==len(activity_masks)<=TRACKS
    signatures=np.repeat(geometry.no_source[None],TRACKS,axis=0).copy()
    beta=np.zeros((TRACKS,TIME),np.float32);truth=[];gray=[];counts=[]
    for k,(s,mask) in enumerate(zip(details,activity_masks)):
        mask=np.asarray(mask,bool);assert mask.shape==(TIME*FRAME,)
        count=int(mask.sum());assert count==s['active_samples']
        counts.append(count);beta[k]=mask.reshape(TIME,FRAME).mean(1)
        assert float(beta[k].astype(float).sum()*FRAME)==count
        if count:
            signatures[k]=direction_signatures([s['direction']])[0]
        if count>=MIN_ACTIVE:truth.append(s['direction'])
        elif count:gray.append(s['direction'])
    return dict(signatures=signatures,beta=beta,truth=truth,gray=gray,active_samples=counts,
                loss_weight=float(not gray),label_version=VERSION)

class FullNarrowBlock(nn.Module):
    def __init__(self,first):
        super().__init__()
        self.first=first
        self.full=nn.LSTM(32 if first else HIDDEN+32,HIDDEN,batch_first=True,bidirectional=True)
        self.project=nn.Linear(2*HIDDEN,HIDDEN)
        self.narrow=nn.LSTM(HIDDEN+32,HIDDEN,batch_first=True,bidirectional=False)
    def forward(self,h,raw):
        b,t,f,_=raw.shape
        x=raw if self.first else torch.cat((h,raw),-1)
        x,_=self.full(x.reshape(b*t,f,-1));x=self.project(x).reshape(b,t,f,HIDDEN)
        x=torch.cat((x,raw),-1).permute(0,2,1,3).reshape(b*f,t,-1)
        x,_=self.narrow(x)
        return x.reshape(b,f,t,HIDDEN).permute(0,2,1,3)

def frequency_pool(x):
    b,t,f,d=x.shape
    return F.avg_pool1d(x.reshape(b*t,f,d).transpose(1,2),POOL,ceil_mode=True).transpose(1,2).reshape(b,t,math.ceil(f/POOL),d)

class DirectPhaseNetwork(nn.Module):
    """Small experimental causal-time full/narrow branch; no input true K/DOA."""
    def __init__(self):
        super().__init__()
        self.first=FullNarrowBlock(True);self.second=FullNarrowBlock(False)
        self.phase=nn.Linear(HIDDEN,TRACKS*15*2)
        self.activity=nn.Linear(HIDDEN,TRACKS)
        # 从低占用初始化，不无条件输出声源。
        nn.init.zeros_(self.activity.weight);nn.init.constant_(self.activity.bias,-6.)
    def forward(self,x):
        b,t,f,c=x.shape
        if (t,f,c)!=(TIME,len(FREQ_BINS),32) or not torch.isfinite(x).all():
            raise ValueError('finite observed native STFT[B,20,419,32] required')
        h=self.first(None,x)
        h=self.second(frequency_pool(h),frequency_pool(x))
        # 只恢复频率轴，不做时间插值，也不访问未来帧。
        h=F.interpolate(h.reshape(b*t,h.shape[2],HIDDEN).transpose(1,2),size=f,mode='linear',align_corners=False).transpose(1,2).reshape(b,t,f,HIDDEN)
        residual=(2*torch.tanh(self.phase(h))).reshape(b,t,f,TRACKS,15,2).permute(0,3,1,2,4,5)
        activity_logits=self.activity(h.mean(2)).permute(0,2,1)
        return dict(residual=residual,activity_logits=activity_logits)

def phase_loss(output,signatures,beta,no_source,loss_weight):
    """Window PIT (six orders), justified by fixed source positions per window.

    This differs from paper frame PIT. Soft BCE is frame occupancy supervision,
    not sound classification. MSE supervises the direct spatial phase residual.
    """
    r=output['residual'];logits=output['activity_logits']
    b,k,t,f,p,c=r.shape
    assert (k,t,f,p,c)==(TRACKS,TIME,len(FREQ_BINS),15,2)
    q=signatures-no_source
    target=beta[:,:,:,None,None,None]*q[:,:,None,:,:,:]
    # 代价按预测轨迹×目标轨迹排列，整窗共用一次排序。
    mse=(r[:,:,None]-target[:,None]).square().mean((3,4,5,6))
    a=logits[:,:,None,:].expand(-1,-1,TRACKS,-1)
    y=beta[:,None].expand(-1,TRACKS,-1,-1)
    activity=F.binary_cross_entropy_with_logits(a,y,reduction='none').mean(-1)
    costs=mse+activity
    per_order=torch.stack([costs[:,torch.arange(TRACKS),list(order)].mean(1) for order in permutations(range(TRACKS))],1)
    best=per_order.min(1).values
    w=loss_weight.to(best.dtype)
    return (best*w).sum()/w.sum().clamp_min(1)

def decode_tracks(residual,occupancy,geometry):
    """Physical full-grid matching; occupancy-derived original20ms window rule.

    Accepts predicted quantities only, no truth/labels/count. No Top16 proposal
    restriction. No model probability calibration is claimed by this prototype.
    """
    r=np.asarray(residual,float);a=np.asarray(occupancy,float)
    assert r.shape==(TRACKS,TIME,len(FREQ_BINS),15,2) and a.shape==(TRACKS,TIME)
    assert np.isfinite(r).all() and np.isfinite(a).all() and np.all((a>=0)&(a<=1))
    proposals=[];centered=geometry.centered.reshape(len(geometry.directions),-1).astype(float)
    for k in range(TRACKS):
        support=float(a[k].sum()*FRAME)
        if support<MIN_ACTIVE:continue
        cross=(a[k,:,None,None,None]*r[k]).sum(0).reshape(-1)
        cost=(np.square(r[k]).sum()+np.square(a[k]).sum()*geometry.norm-2*(centered@cross))/(TIME*len(FREQ_BINS)*15*2)
        i=int(np.argmin(cost));point=geometry.directions[i].tolist()
        proposals.append(dict(track=k,direction=point,estimated_active_samples=support,
                              phase_template_MSE=float(max(cost[i],0))))
    outputs=[]
    for row in sorted(proposals,key=lambda v:(-v['estimated_active_samples'],v['track'])):
        # 这是独立轨迹，而不是同一谱的相邻峰；仅合并
        # 完全相同的网格单元，保留不同的近邻源轨迹输出。
        if any(angular_distance_deg(*row['direction'],*p['direction'])<=1e-6 for p in outputs):continue
        outputs.append(dict(row,source_id=len(outputs)+1))
    return outputs

def budget(model):
    f=len(FREQ_BINS);g=math.ceil(f/POOL)
    full=lambda n,d:TIME*n*2*4*(d+HIDDEN)*HIDDEN
    narrow=lambda n:TIME*n*4*(HIDDEN+32+HIDDEN)*HIDDEN
    mac=full(f,32)+full(g,HIDDEN+32)+narrow(f)+narrow(g)
    mac+=TIME*(f+g)*(2*HIDDEN*HIDDEN)+TIME*f*HIDDEN*(TRACKS*15*2)+TIME*HIDDEN*TRACKS
    count=sum(p.numel() for p in model.parameters())
    return dict(parameters=count,FP32_parameter_bytes=count*4,dense_neural_MAC_per_window=mac,
        input_shape=[TIME,f,32],phase_output_shape=[TRACKS,TIME,f,15,2],
        input_float32_bytes=TIME*f*32*4,phase_output_float32_bytes=TRACKS*TIME*f*15*2*4,
        second_block_frequency_bins=g,window_seconds=TIME*FRAME/FS,
        excludes='FFT, normalization, recurrent activations, pooling/interpolation, PIT, geometry matching, IO; not FPGA timing',
        paper_architecture_reproduction=False,trained=False)
