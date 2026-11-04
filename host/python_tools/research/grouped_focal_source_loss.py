"""Training-only symmetric focal modulation; original class-balanced groups.

Lin et al. ICCV2017 Eq.4, adapted to this detector's existing window/group
reduction. Fixed gamma2 for the planned trial; gamma0 is only the BCE identity
check. No RetinaNet alpha, label, decoder or inference change is introduced.
"""
import torch
from torch.nn import functional as F

VERSION = 'ordinary_grouped_symmetric_focal_gamma2_20261013'


def grouped_focal_loss(logits, targets, valid, gamma=2.):
    if logits.ndim != 2 or logits.shape != targets.shape or valid.shape != targets.shape:
        raise ValueError('matching [B,C] tensors required')
    if not logits.is_floating_point() or not torch.isfinite(logits).all():
        raise ValueError('finite floating logits required')
    if valid.dtype != torch.bool or not torch.all((targets == -1) | (targets == 0) | (targets == 1)):
        raise ValueError('boolean validity and discrete {-1,0,1} labels required')
    if gamma not in (0., 2.):
        raise ValueError('only fixed gamma2 trial or gamma0 identity check allowed')
    binary = targets.clamp_min(0).to(logits.dtype)
    terms = F.binary_cross_entropy_with_logits(logits, binary, reduction='none')
    if gamma:
        # 1-p_t用sigmoid(-有符号logit)计算，避免对数及抵消误差。
        signed = logits * (2 * binary - 1)
        terms = terms * torch.sigmoid(-signed).square()
    windows = []
    for i in range(len(logits)):
        groups = [terms[i][valid[i] & (targets[i] == value)].mean()
                  for value in (1, 0) if (valid[i] & (targets[i] == value)).any()]
        if groups:
            windows.append(torch.stack(groups).mean())
    return torch.stack(windows).mean() if windows else logits.sum() * 0
