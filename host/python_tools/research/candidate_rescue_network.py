"""One registered candidate-shared two-head causal temporal PC research model.

Only observable [batch,candidate,20,88] features enter the model. Directions,
identities, source count/activity, components and labels are not model inputs.
Window mean pooling and full-window proposals retain the 1.048576-second wait.
"""
from __future__ import annotations

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION = "mic16_candidate_rescue_tcn_3026_v1_20261008"
FRAMES, FEATURES, HIDDEN, CAP = 20, 88, 16, 16


class CandidateRescueTCN(nn.Module):
    def __init__(self):
        super().__init__()
        self.projection = nn.Linear(FEATURES, HIDDEN)
        self.temporal1 = nn.Conv1d(HIDDEN, HIDDEN, 3, dilation=1)
        self.temporal2 = nn.Conv1d(HIDDEN, HIDDEN, 3, dilation=2)
        self.heads = nn.Linear(HIDDEN, 2)

    def temporal_features(self, features):
        if features.ndim != 4 or tuple(features.shape[-2:]) != (FRAMES, FEATURES):
            raise ValueError("features require [batch,candidate,20,88]")
        b, c = features.shape[:2]
        if b < 1 or not 1 <= c <= CAP or not features.is_floating_point() or not torch.isfinite(features).all():
            raise ValueError("finite floating features and1..16 candidates required")
        x = F.relu(self.projection(features)).reshape(b*c, FRAMES, HIDDEN).transpose(1, 2)
        x = F.relu(self.temporal1(F.pad(x, (2, 0))))
        x = F.relu(self.temporal2(F.pad(x, (4, 0))))
        return x.transpose(1, 2).reshape(b, c, FRAMES, HIDDEN)

    def forward(self, features):
        return self.heads(self.temporal_features(features).mean(dim=2))


def balanced_window_loss(logits, labels, candidate_valid):
    """Per window/head: mean positive/negative groups, then groups equally.

    Missing groups contribute nothing; all-ignored heads are omitted from the
    window's head mean. Invalid candidates and label -1 have zero gradient.
    This function has no optimizer, fitted normalization or data augmentation.
    """
    if logits.ndim != 3 or logits.shape[-1] != 2 or labels.shape != logits.shape or candidate_valid.shape != logits.shape[:2]:
        raise ValueError("logits/labels require [B,C,2], valid requires[B,C]")
    if not torch.isfinite(logits).all() or not torch.all((labels == -1) | (labels == 0) | (labels == 1)):
        raise ValueError("finite logits and labels in{-1,0,1} required")
    terms = F.binary_cross_entropy_with_logits(logits, labels.clamp_min(0).to(logits.dtype), reduction="none")
    window_values = []
    for i in range(len(logits)):
        head_values = []
        for head in range(2):
            groups = []
            for value in (0, 1):
                keep = candidate_valid[i].bool() & (labels[i, :, head] == value)
                if keep.any():
                    groups.append(terms[i, :, head][keep].mean())
            if groups:
                head_values.append(torch.stack(groups).mean())
        if head_values:
            window_values.append(torch.stack(head_values).mean())
    return torch.stack(window_values).mean() if window_values else logits.sum()*0


def model_budget(model):
    per_layer = {name: sum(p.numel() for p in layer.parameters()) for name, layer in
                 [("projection",model.projection),("temporal1",model.temporal1),("temporal2",model.temporal2),("heads",model.heads)]}
    return dict(parameters=sum(p.numel() for p in model.parameters()), per_layer_parameters=per_layer,
                fp32_weight_bytes=4*sum(p.numel() for p in model.parameters()),
                dense_network_mac_per_window=CAP*FRAMES*(FEATURES*HIDDEN+2*HIDDEN*HIDDEN*3)+CAP*HIDDEN*2,
                effective_temporal_receptive_field_frames=1+2+4,
                excludes="PHAT/maps/DAS/spectral features/activation/copy/IO", full_window_wait_seconds=40960/39062.5)


def supplement_v1(baseline, directions, probabilities, candidate_valid, threshold, enabled=True):
    """Preserve ordered baseline exactly; append both-head-positive directions.

    Fixed score=min(two Sigmoids), score-descending/original-index ties,
    10deg duplicate exclusion and8 total outputs. No true K/identity/labels.
    Disabled branch returns before accessing any candidate/model fields.
    """
    original = [list(p) for p in baseline]
    if not enabled:
        return original, []
    d, p, valid = np.asarray(directions,float), np.asarray(probabilities,float), np.asarray(candidate_valid,bool)
    if d.ndim != 2 or d.shape[1] != 2 or p.shape != d.shape or valid.shape != (len(d),) or not 0 <= threshold <= 1:
        raise ValueError("directions/probabilities[C,2], valid[C], threshold[0,1] required")
    if len(d)>CAP or len(original)>8 or not np.isfinite(d).all() or not np.isfinite(p).all() or np.any((p<0)|(p>1)):
        raise ValueError("finite probabilities[0,1], cap16 and baseline cap8 required")
    score = p.min(axis=1)
    ordered = sorted((i for i in range(len(d)) if valid[i] and score[i]>=threshold), key=lambda i:(-score[i],i))
    out = original.copy(); additions = []
    for i in ordered:
        if len(out)>=8:
            break
        if any(angular_distance_deg(*d[i],*point)<=10 for point in out):
            continue
        point=d[i].tolist();out.append(point)
        additions.append(dict(candidate_index=i,direction=point,score=float(score[i]),head_scores=p[i].tolist()))
    return out, additions


def match_directions(outputs, truths):
    """Maximum cardinality then minimum summed angle, DP over truth subsets."""
    costs=np.array([[angular_distance_deg(*a,*b) for b in truths] for a in outputs],float).reshape(len(outputs),len(truths))
    states={0:(0.,())}
    for i in range(len(outputs)):
        updated=states.copy()
        for mask,(cost,pairs) in states.items():
            for j in range(len(truths)):
                if not mask&(1<<j) and costs[i,j]<=10:
                    key=mask|(1<<j);value=(cost+float(costs[i,j]),pairs+((i,j),))
                    if key not in updated or value<updated[key]:updated[key]=value
        states=updated
    mask,(cost,pairs)=min(states.items(),key=lambda item:(-item[0].bit_count(),item[1]))
    return [dict(output=i,truth=j,error_deg=float(costs[i,j])) for i,j in pairs]


def score_window(outputs, truths, gray_directions=()):
    pairs=match_directions(outputs,truths);matched={p['output'] for p in pairs}
    ignored=[i for i,p in enumerate(outputs) if i not in matched and any(angular_distance_deg(*p,*g)<=15 for g in gray_directions)]
    tp=len(pairs);fp=len(outputs)-len(ignored)-tp
    return dict(tp=tp,fp=fp,fn=len(truths)-tp,pairs=pairs,ignored_output_indices=ignored,
                raw_output_count=len(outputs),scored_output_count=len(outputs)-len(ignored),truth_count=len(truths))


def aggregate(rows):
    tp=sum(r['tp'] for r in rows);fp=sum(r['fp'] for r in rows);fn=sum(r['fn'] for r in rows)
    errors=[p['error_deg'] for r in rows for p in r['pairs']]
    precision=tp/(tp+fp) if tp+fp else 0.;recall=tp/(tp+fn) if tp+fn else 0.
    return dict(windows=len(rows),tp=tp,fp=fp,fn=fn,precision=precision,recall=recall,
                f1=2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else 0.,
                matched_mae_deg=float(np.mean(errors)) if errors else None,
                matched_p95_deg=float(np.percentile(errors,95)) if errors else None,
                ignored_gray_outputs=sum(len(r['ignored_output_indices']) for r in rows),
                clear_zero_nonempty_windows=sum(r.get('clear_zero',False) and r['scored_output_count']>0 for r in rows),
                source_count_correct_windows=sum(r['scored_output_count']==r['truth_count'] for r in rows))
