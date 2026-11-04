"""Untrained C16 supervision prototype: close the active-neighbor ignore hole.

Reuses frozen class-free geometric one-to-one matching and source activity.
Only previously ignored, valid, unassigned active-source neighbors become
no-object negatives. Short/ambiguous emissions and padding stay ignored.
No inference or trained checkpoint lives here; do not alter baseline labels.
"""
import numpy as np
from acoustic_camera.valid_source_detection import angular_distance_deg
from research.all_source_detection import candidate_targets,GRAY_DEG

VERSION='mic16_C16_active_neighbor_no_object_supervision_prototype_20261012'


def no_object_targets(directions,valid,record):
    d=np.asarray(directions,float);v=np.asarray(valid,bool)
    if d.ndim!=2 or d.shape[1]!=2 or not 1<=len(d)<=16:
        raise ValueError('This prototype is fixed to retained C16, not C32')
    baseline=candidate_targets(d,v,record)
    result=dict(baseline);labels=baseline['labels'].copy();changed=[]
    for c in np.flatnonzero(v&(labels==-1)):
        # 短至1～781个样本的声源仍保留真实标注不确定性。
        if any(angular_distance_deg(*d[c],*g)<=GRAY_DEG for g in baseline['gray']):
            continue
        if any(angular_distance_deg(*d[c],*t)<=GRAY_DEG for t in baseline['truth']):
            labels[c]=0;changed.append(int(c))
    result.update(labels=labels,previously_ignored_active_candidates=changed,version=VERSION)
    assert np.array_equal(labels==1,baseline['labels']==1)
    assert np.array_equal(labels[~v],baseline['labels'][~v])
    return result
