"""Observed-only distant-candidate contrasts; retain all original C16 features.

This is an engineering hypothesis inspired by subband localization literature,
not a reimplementation of a published network. No TF winner or source-count gate.
"""
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from research.multisource_split_network import DetectionNetwork
from research.candidate_joint_validity_network import observed_temporal_statistics
from research.candidate_neighborhood_tcn import neighborhood_mask

VERSION = 'c16_distant_subband_contrast_20261014'


def subband_contrasts(features, directions, valid):
    """r(c,t,z)-max r(other,t,z), excluding directions within 15 degrees.

Raw signed contrasts are kept even when negative. Invalid or absent competitors
produce zero. Geometry, PHAT responses and valid masks are the only inputs.
"""
    x=np.asarray(features,np.float32);d=np.asarray(directions,np.float64);v=np.asarray(valid,bool)
    assert x.ndim==4 and x.shape[-2:]==(20,106)
    assert d.shape==(*x.shape[:2],2) and v.shape==x.shape[:2]
    a,e=np.deg2rad(d[...,0]),np.deg2rad(d[...,1])
    u=np.stack((np.cos(e)*np.sin(a),np.sin(e),np.cos(e)*np.cos(a)),-1)
    angle=np.rad2deg(np.arccos(np.clip(u@u.swapaxes(-2,-1),-1.,1.)))
    peers=(angle>15.) & v[:,:,None] & v[:,None,:]
    output=np.zeros((*x.shape[:3],24),np.float32)
    for b in range(len(x)):
        for c in range(x.shape[1]):
            ids=np.flatnonzero(peers[b,c])
            if len(ids):output[b,c]=x[b,c,:,:24]-x[b,ids,:,:24].max(0)
    assert np.isfinite(output).all()
    return output


class CompetitionNetwork(DetectionNetwork):
    def __init__(self):
        super().__init__()
        # 精确保留全部原随机参数及随机数发生器状态。
        state=torch.get_rng_state()
        self.competition_project=nn.Linear(24,32,bias=False)
        nn.init.zeros_(self.competition_project.weight)
        torch.set_rng_state(state)

    def forward(self, features, directions, valid):
        b,c,t,f=features.shape
        assert (t,f)==(20,130) and valid.shape==(b,c)
        raw=features[...,:106]
        h=F.relu(self.project(raw)+self.competition_project(features[...,106:]))
        h=h.reshape(b*c,t,32).transpose(1,2)
        for layer,dilation in zip(self.temporal,(1,2,4)):
            h=F.relu(h+layer(F.pad(h,(2*dilation,0))))
        h=h.reshape(b,c,32,t)
        own=torch.cat((h.mean(-1),h.amax(-1),observed_temporal_statistics(raw[...,:88])),dim=-1)
        neighbors=neighborhood_mask(directions,valid)
        count=neighbors.sum(-1,keepdim=True);exists=count>0
        mean=neighbors.to(own.dtype)@own/count.clamp_min(1)
        maximum=own.unsqueeze(1).expand(-1,c,-1,-1).masked_fill(~neighbors.unsqueeze(-1),-torch.inf).amax(2)
        maximum=torch.where(exists,maximum,torch.zeros_like(maximum))
        context=torch.cat((own,torch.where(exists,own-mean,torch.zeros_like(own)),
            torch.where(exists,own-maximum,torch.zeros_like(own)),count.to(own.dtype)/15,exists.to(own.dtype)),dim=-1)
        return self.head(context).squeeze(-1).masked_fill(~valid,-20.)

    def numpy_weights(self):
        # 独立C16 NumPy评估器支持任意投影宽度。
        state={'detector.'+n:p.detach().cpu() for n,p in self.state_dict().items() if n!='competition_project.weight'}
        state['detector.project.weight']=torch.cat((self.project.weight.detach().cpu(),
            self.competition_project.weight.detach().cpu()),dim=1)
        return state
