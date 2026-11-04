"""Single-factor ablation: geometric assignment, full unmatched negatives.

Architecture, inference and balanced BCE are identical to the completed
score-matched trial. Only the rule choosing one positive per truth changes.
Ground truth is confined to this training target builder.
"""
from __future__ import annotations
import numpy as np
import torch
from research.all_source_detection import candidate_targets, all_source_loss, GRAY_DEG
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION='mic16_all_source_geometric_matched_supervision_v3_20261011'


def geometric_set_targets(directions,valid,record):
    """Maximum cardinality, then minimum total angular error within10 degrees.

Use the baseline's sealed geometric matcher. Unmatched valid candidates,
including near-source alternatives, are negative as in the failed v2 trial.
Short-emission gray/padding remain ignored; positives override local gray.
No logits, confidence threshold, source class or identity are used.
"""
    d=np.asarray(directions,float);v=np.asarray(valid,bool)
    if not 1<=len(d)<=16:raise ValueError('1..16 candidates required')
    matched=candidate_targets(d,v,record)
    labels=np.full(len(d),-1,np.int8);labels[v]=0
    for i in np.flatnonzero(v):
        if any(angular_distance_deg(*d[i],*g)<=GRAY_DEG for g in matched['gray']):labels[i]=-1
    for pair in matched['matching']:labels[pair['candidate']]=1
    return dict(matched,labels=labels)


def geometric_matching_loss(logits,directions,valid,records):
    if (logits.ndim!=2 or directions.shape!=(*logits.shape,2) or valid.shape!=logits.shape or len(records)!=len(logits)):
        raise ValueError('matching [B,C] logits, [B,C,2] directions, valid and records required')
    ds=directions.detach().cpu().numpy();vs=valid.detach().cpu().numpy()
    y=torch.as_tensor(np.stack([geometric_set_targets(ds[i],vs[i],r)['labels'] for i,r in enumerate(records)]),device=logits.device)
    return all_source_loss(logits,y,valid)
