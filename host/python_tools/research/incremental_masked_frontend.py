"""Share full maps; only TF maximum search domain changes between branches."""
import sys
from pathlib import Path
import numpy as np
from research.incremental_source_search import masked_argmax
from research.candidate_rescue_features import extract_candidate_features,causal_candidate_beam
from research.directional_tf_mask import observed_features
from research.multisource_split_network import detection_observations
from research.candidate_subband_competition import subband_contrasts
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/verification'))
from audit_pre_gate_candidate_coverage import clusters,fixed_bounded_pool


def rescan_votes(maps,primary,grid,allowed_separation,search_mask):
    x=np.asarray(maps);g=np.asarray(grid);mask=np.asarray(search_mask,bool)
    if x.shape!=(20,24,13,25) or g.shape!=(325,2) or primary.shape!=(480,9) or mask.shape!=(325,):raise ValueError('fixed TF/grids/votes required')
    if not mask.any():return None
    out=primary.copy()
    for j,row in enumerate(out):
        t,z=int(row[0]),int(row[1]);score=x[t,z].ravel();winner=masked_argmax(score,mask)
        other=allowed_separation[winner]&mask
        # 没有第二个独立方向时不能虚构正峰对比度。
        gap=max(0.,float(score[winner])-float(score[other].max())) if other.any() else 0.
        row[4:6]=g[winner];row[7]=float(score[winner]);row[8]=gap
    return out


def observed_candidate_features(frames,reference,votes):
    features=np.zeros((16,20,106),np.float32);d=np.zeros((16,2),np.float32);v=np.zeros(16,bool)
    if votes is not None:
        pool=fixed_bounded_pool(clusters(votes,np.ones(480,bool),10.))
        directions=np.asarray([c['direction'] for c in pool],float).reshape(-1,2);count=len(directions)
        if count:
            physical,_=extract_candidate_features(frames,reference,directions,votes)
            beams=np.stack([causal_candidate_beam(frames,p)[0] for p in directions])
            spectral,_,_=observed_features(frames,directions,reference,beams)
            features[:count]=detection_observations(physical,spectral);d[:count]=directions;v[:count]=True
    contrast=subband_contrasts(features[None],d[None],v[None])[0]
    return dict(features=np.concatenate((features,contrast),-1),directions=d,valid=v,votes=votes)


def observed_search_candidates(obs,scores):
    # 两个分支使用完全相同、事先声明的视场有效条件。
    candidates=[];outside=0
    for j in np.flatnonzero(obs['valid']):
        a,e=map(float,obs['directions'][j]);s=float(scores[j])
        if s<.65:continue
        if abs(a)>60+1e-9 or abs(e)>30+1e-9:outside+=1;continue
        candidates.append(dict(direction=[float(np.clip(a,-60,60)),float(np.clip(e,-30,30))],score=s,candidate_index=int(j)))
    return candidates,outside
