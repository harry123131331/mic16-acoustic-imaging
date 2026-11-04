"""Independent NumPy phase forward and null-aware physical track decoding.

The v1 feasibility prototype stays sealed. The decoder now compares its best
direction fit with the no-source fit before emitting a track. Occupancy alone
must not turn a zero phase residual into a point source. No true K/labels enter.
"""
import numpy as np
from research import direct_path_ipd as phase
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION='mic16_direct_ipd_null_distance_decoder_v1_20261012'

def sigmoid(x):return 1/(1+np.exp(-np.clip(x,-80,80)))

def decode(residual,occupancy,geometry,return_maps=False):
    r=np.asarray(residual,float);a=np.asarray(occupancy,float)
    assert r.shape==(3,20,len(phase.FREQ_BINS),15,2) and a.shape==(3,20)
    assert np.isfinite(r).all() and np.isfinite(a).all() and np.all((a>=0)&(a<=1))
    d=geometry.centered.reshape(len(geometry.directions),-1).astype(float)
    proposals=[];maps=[]
    for k in range(3):
        support=float(a[k].sum()*phase.FRAME)
        null=float(np.square(r[k]).mean())
        cross=(a[k,:,None,None,None]*r[k]).sum(0).reshape(-1)
        costs=(np.square(r[k]).sum()+np.square(a[k]).sum()*geometry.norm-2*(d@cross))/(20*len(phase.FREQ_BINS)*15*2)
        i=int(costs.argmin());best=float(max(costs[i],0));admitted=bool(support>=phase.MIN_ACTIVE and best<null)
        row=dict(track=k,direction=geometry.directions[i].tolist(),estimated_active_samples=support,
                 phase_template_MSE=best,no_source_MSE=null,admitted=admitted)
        maps.append(dict(row,grid_phase_MSE=costs.tolist()))
        if admitted:proposals.append(row)
    outputs=[]
    for row in sorted(proposals,key=lambda p:(-p['estimated_active_samples'],p['track'])):
        if any(angular_distance_deg(*row['direction'],*q['direction'])<=1e-6 for q in outputs):continue
        outputs.append(dict(row,source_id=len(outputs)+1))
    return (outputs,maps) if return_maps else outputs

def numpy_forward(features,state):
    """Explicit LSTM gates; no torch call or trained-model forward reuse."""
    w={k:np.asarray(v.detach().cpu().numpy() if hasattr(v,'detach') else v,dtype=float) for k,v in state.items()}
    def linear(x,name):return x@w[name+'.weight'].T+w[name+'.bias']
    def lstm(x,name,bidirectional):
        result=[];batch,length,_=x.shape
        for reverse in range(2 if bidirectional else 1):
            suffix='_l0_reverse' if reverse else '_l0'
            wi=w[name+'.weight_ih'+suffix];wh=w[name+'.weight_hh'+suffix]
            bias=w[name+'.bias_ih'+suffix]+w[name+'.bias_hh'+suffix]
            h=np.zeros((batch,phase.HIDDEN));c=np.zeros_like(h);y=np.empty((batch,length,phase.HIDDEN))
            for t in (range(length-1,-1,-1) if reverse else range(length)):
                i,f,g,o=np.split(x[:,t]@wi.T+h@wh.T+bias,4,axis=-1)
                c=sigmoid(f)*c+sigmoid(i)*np.tanh(g);h=sigmoid(o)*np.tanh(c);y[:,t]=h
            result.append(y)
        return np.concatenate(result,axis=-1)
    def block(h,raw,name):
        b,t,f,_=raw.shape;x=raw if h is None else np.concatenate((h,raw),-1)
        x=linear(lstm(x.reshape(b*t,f,-1),name+'.full',True),name+'.project').reshape(b,t,f,phase.HIDDEN)
        x=np.concatenate((x,raw),-1).transpose(0,2,1,3).reshape(b*f,t,-1)
        return lstm(x,name+'.narrow',False).reshape(b,f,t,phase.HIDDEN).transpose(0,2,1,3)
    def pool(x):return np.stack([x[:,:,i:i+4].mean(2) for i in range(0,x.shape[2],4)],2)
    raw=np.asarray(features,float);b,t,f,c=raw.shape;assert (t,f,c)==(20,len(phase.FREQ_BINS),32)
    h=block(None,raw,'first');h=block(pool(h),pool(raw),'second')
    positions=np.clip((np.arange(f)+.5)*h.shape[2]/f-.5,0,h.shape[2]-1)
    left=np.floor(positions).astype(int);right=np.minimum(left+1,h.shape[2]-1);frac=positions-left
    h=h[:,:,left]*(1-frac)[None,None,:,None]+h[:,:,right]*frac[None,None,:,None]
    r=(2*np.tanh(linear(h,'phase'))).reshape(b,t,f,3,15,2).transpose(0,3,1,2,4,5)
    logits=linear(h.mean(2),'activity').transpose(0,2,1)
    return dict(residual=r,activity_logits=logits)
