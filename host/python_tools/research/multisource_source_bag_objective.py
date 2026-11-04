"""Source-level positive selection, negative exclusion, and duplicate penalty."""
import torch
from torch.nn import functional as F

VERSION='mic16_source_bag_selection_loss_v1_20261009'


def terms(logits,valid,bags,negative_groups):
    if logits.ndim!=2 or valid.shape!=logits.shape or bags.shape!=(len(logits),8,logits.shape[1]) or negative_groups.shape!=(len(logits),3,logits.shape[1]):
        raise ValueError('logits[B,C], valid[B,C], bags[B,8,C], negative_groups[B,3,C] required')
    if any(x.dtype!=torch.bool for x in (valid,bags,negative_groups)) or not torch.isfinite(logits).all():
        raise ValueError('Boolean masks and finite logits required')
    eligible=bags.any(1);negative=negative_groups.any(1)
    if torch.any(bags.sum(1)>1) or torch.any(negative_groups.sum(1)>1) or torch.any(eligible&negative) or torch.any((eligible|negative)&~valid):
        raise ValueError('Disjoint valid source bags and negative groups required')
    probability=torch.sigmoid(logits);zero=logits.sum()*0
    totals=[];positives=[];false=[];duplicates=[]
    for i in range(len(logits)):
        covered=bags[i].any(-1)
        if not covered.any() and not negative[i].any():
            continue
        numerator=zero;denominator=0.
        positive=duplicate=zero
        if covered.any():
            positive=torch.stack([F.softplus(-logits[i,b].max()) for b in bags[i,covered]]).mean()
            duplicate=torch.stack([F.relu(probability[i,b].sum()-1).square() for b in bags[i,covered]]).mean()
            numerator=numerator+3*positive;denominator+=3
        for group in negative_groups[i]:
            if group.any():
                loss=F.softplus(logits[i,group])
                numerator=numerator+torch.topk(loss,min(3,len(loss)),sorted=False).values.mean()
                denominator+=1
        false_mass=probability[i,negative[i]].sum()
        totals.append(numerator/denominator+.25*(false_mass+duplicate))
        positives.append(positive);false.append(false_mass);duplicates.append(duplicate)
    mean=lambda x:torch.stack(x).mean() if x else zero
    return dict(total=mean(totals),source_positive_loss=mean(positives),
        soft_false_alarm_mass=mean(false),source_duplicate_penalty=mean(duplicates))
