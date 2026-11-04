"""TRAIN-only source bags aligned with the existing10-degree evaluation rule."""
import numpy as np
from scipy.optimize import linear_sum_assignment
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION='mic16_source_bag_accepted10deg_v1_20261009'
MAX_SOURCES=8


def build(directions,valid,old_labels,source_states):
    d,v,y=np.asarray(directions),np.asarray(valid,bool),np.asarray(old_labels)
    if d.shape!=(16,2) or v.shape!=(16,) or y.shape!=(16,2) or not np.isfinite(d).all():
        raise ValueError('Frozen directions[16,2], valid[16], old labels[16,2] required')
    on=[s['direction'] for s in source_states if s['role']=='speech' and s['state']=='ON_WINDOW_OCCUPANCY']
    short=[s['direction'] for s in source_states if s['role']=='speech' and s['state']=='GRAY_SHORT_EMISSION']
    if len(on)>MAX_SOURCES:
        raise ValueError('At most8 annotated sources supported')
    dist=np.array([[angular_distance_deg(*p,*t) for p in d] for t in on]).reshape(len(on),16)
    allowed=(dist<=10)&v[None,:]
    bags=np.zeros((MAX_SOURCES,16),bool)
    if len(on):
        # 先保留最大覆盖的一对一分配，再将其余
        # 合格候选分给最近源；正例组互不相交，
        # 且接收区域重叠时不降低可达到的真源覆盖。
        cost=np.concatenate((np.where(allowed,dist,1e6),np.full((len(on),len(on)),1000.)),axis=1)
        rows,cols=linear_sum_assignment(cost)
        owner=np.argmin(np.where(allowed,dist,np.inf),axis=0)
        for source,candidate in zip(rows,cols):
            if candidate<16 and allowed[source,candidate]:
                owner[candidate]=source
        for candidate in np.flatnonzero(allowed.any(0)):
            bags[owner[candidate],candidate]=True
    eligible=bags.any(0)
    near_short=np.array([any(angular_distance_deg(*p,*t)<=15 for t in short) for p in d])
    negative=v&~eligible&~near_short
    nuisance=negative&(y[:,0]==1)
    near_on=allowed.any(0) if not len(on) else (dist<=15).any(0)
    groups=np.stack((nuisance,negative&~nuisance&near_on,negative&~nuisance&~near_on))
    assert np.array_equal(groups.sum(0),negative.astype(int))
    assert np.max(bags.sum(0))<=1 and not np.any(eligible&negative)
    changed=np.flatnonzero(eligible&v&(y[:,1]==0))
    return dict(bags=bags,negative_groups=groups,annotated_sources=len(on),covered_sources=int(bags.any(1).sum()),
        former_negative_accepted_candidates=changed.tolist(),gray_ignored_candidates=np.flatnonzero(v&~eligible&near_short).tolist())
