"""Research-only configurable angular duplicate suppression, no truth inputs."""
import numpy as np
from acoustic_camera.valid_source_detection import angular_distance_deg
def decode(directions, scores, valid, radius_deg, threshold=.65):
    d=np.asarray(directions,float);p=np.asarray(scores,float);v=np.asarray(valid,bool)
    assert d.ndim==2 and d.shape[1]==2 and p.shape==v.shape==(len(d),) and 1<=len(d)<=16
    assert np.isfinite(d).all() and np.isfinite(p).all() and np.all((p>=0)&(p<=1))
    assert radius_deg in (10.,15.) and threshold==.65
    chosen=[]
    for c in sorted((j for j in range(len(d)) if v[j] and p[j]>=threshold),key=lambda j:(-p[j],j)):
        if any(angular_distance_deg(*d[c],*d[j])<=radius_deg for j in chosen):continue
        chosen.append(c)
        if len(chosen)==8:break
    return [dict(source_id=i+1,azimuth_deg=d[c,0],elevation_deg=d[c,1],
                 detection_score=float(p[c]),candidate_index=c) for i,c in enumerate(chosen)]
