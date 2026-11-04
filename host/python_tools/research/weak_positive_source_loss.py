"""One-factor TRAIN positive aggregation; inference and labels unchanged.

Keep equal positive/negative window groups. Within an existing positive group,
use half mean and half worst positive BCE. This is an OHEM-inspired engineering
hypothesis, not a published acoustic method or demonstrated performance gain.
"""
import torch
from torch.nn import functional as F

VERSION='ordinary_assigned_positive_mean_max_half_20261012'


def weak_positive_loss(logits,targets,valid):
    if logits.shape!=targets.shape or valid.shape!=targets.shape:
        raise ValueError('matching [B,C] tensors required')
    if not torch.isfinite(logits).all() or not torch.all((targets==-1)|(targets==0)|(targets==1)):
        raise ValueError('finite logits and discrete {-1,0,1} labels required')
    terms=F.binary_cross_entropy_with_logits(logits,targets.clamp_min(0).to(logits.dtype),reduction='none')
    windows=[]
    for i in range(len(logits)):
        positive=terms[i][valid[i].bool()&(targets[i]==1)]
        negative=terms[i][valid[i].bool()&(targets[i]==0)]
        groups=[]
        if len(positive):groups.append(.5*positive.mean()+.5*positive.amax())
        if len(negative):groups.append(negative.mean())
        if groups:windows.append(torch.stack(groups).mean())
    return torch.stack(windows).mean() if windows else logits.sum()*0
