"""PC inference geometry in FP64 with a closed15-degree boundary.

Weights/features/neural arithmetic remainFP32. The1e-9 degree tolerance closes
mathematically exact borders under FP64 roundoff; it is not a tuned NMS radius.
"""
import math
import numpy as np
import torch
from torch.nn import functional as F
from research.candidate_joint_validity_network import observed_temporal_statistics
RADIUS_DEG=15.
GEOMETRY_BOUNDARY_TOLERANCE_DEG=1e-9

def stable_neighbor_mask(directions,valid):
    rad=directions.to(torch.float64)*(math.pi/180)
    a,e=rad.unbind(-1)
    u=torch.stack((e.cos()*a.sin(),e.sin(),e.cos()*a.cos()),-1)
    mask=u@u.transpose(-1,-2)>=math.cos(math.radians(RADIUS_DEG+GEOMETRY_BOUNDARY_TOLERANCE_DEG))
    return mask & valid[:,:,None] & valid[:,None,:] & ~torch.eye(directions.shape[1],dtype=torch.bool,device=directions.device)[None]

def stable_score(model,features,directions,valid):
    b,c,t,f=features.shape;assert t==20 and f in (106,130)
    assert features.dtype==torch.float32
    raw=features[...,:106];h=model.project(raw)
    if f==130:h=h+model.competition_project(features[...,106:])
    h=F.relu(h).reshape(b*c,t,32).transpose(1,2)
    for layer,dilation in zip(model.temporal,(1,2,4)):h=F.relu(h+layer(F.pad(h,(2*dilation,0))))
    h=h.reshape(b,c,32,t)
    own=torch.cat((h.mean(-1),h.amax(-1),observed_temporal_statistics(raw[...,:88])),dim=-1)
    neighbors=stable_neighbor_mask(directions,valid);count=neighbors.sum(-1,keepdim=True);exists=count>0
    mean=neighbors.to(own.dtype)@own/count.clamp_min(1)
    maximum=own.unsqueeze(1).expand(-1,c,-1,-1).masked_fill(~neighbors.unsqueeze(-1),-torch.inf).amax(2)
    maximum=torch.where(exists,maximum,torch.zeros_like(maximum))
    context=torch.cat((own,torch.where(exists,own-mean,torch.zeros_like(own)),
        torch.where(exists,own-maximum,torch.zeros_like(own)),count.to(own.dtype)/15,exists.to(own.dtype)),dim=-1)
    return model.head(context).squeeze(-1).masked_fill(~valid,-20.)

def numpy_detector_stable(x,directions,valid,state):
    """Independent NumPy arithmetic and acos geometry, same declared boundary."""
    w={k:v.numpy().astype(float) for k,v in state.items() if k.startswith('detector.')}
    h=np.maximum(x@w['detector.project.weight'].T+w['detector.project.bias'],0)
    for number,dilation in enumerate((1,2,4)):
        name=f'detector.temporal.{number}';out=np.broadcast_to(w[name+'.bias'],h.shape).copy()
        for tap in range(3):
            delay=(2-tap)*dilation
            if delay:out[:,:,delay:]+=h[:,:,:-delay]@w[name+'.weight'][:,:,tap].T
            else:out+=h@w[name+'.weight'][:,:,tap].T
        h=np.maximum(h+out,0)
    bands=x[...,48:80];power=10**(4*bands);p=power/power.sum(-1,keepdims=True)
    entropy=-(p*np.log(p)).sum(-1)/np.log(32);flatness=np.exp(np.log(power).mean(-1))/power.mean(-1)
    stats=np.stack((x[...,86].std(-1),bands.std(-2).mean(-1),np.abs(np.diff(bands,axis=-2)).mean((-2,-1)),
        entropy.mean(-1),entropy.std(-1),flatness.mean(-1),flatness.std(-1)),axis=-1)
    own=np.concatenate((h.mean(2),h.max(2),stats),axis=-1);context=np.zeros((*x.shape[:2],215))
    for i in range(len(x)):
        a,e=np.deg2rad(np.asarray(directions[i],float)).T
        u=np.c_[np.cos(e)*np.sin(a),np.sin(e),np.cos(e)*np.cos(a)]
        distance=np.rad2deg(np.arccos(np.clip(u@u.T,-1,1)))
        for c in range(x.shape[1]):
            ids=[j for j in range(x.shape[1]) if c!=j and valid[i,c] and valid[i,j] and distance[c,j]<=RADIUS_DEG+GEOMETRY_BOUNDARY_TOLERANCE_DEG]
            context[i,c,:71]=own[i,c]
            if ids:context[i,c,71:142]=own[i,c]-own[i,ids].mean(0);context[i,c,142:213]=own[i,c]-own[i,ids].max(0);context[i,c,213:]=[len(ids)/15,1]
    for layer in (0,2,4):
        context=context@w[f'detector.head.{layer}.weight'].T+w[f'detector.head.{layer}.bias']
        if layer!=4:context=np.maximum(context,0)
    return np.where(valid,context.squeeze(-1),-20.)
