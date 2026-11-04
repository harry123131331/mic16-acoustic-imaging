"""Training-only selection objective; the sealed 3393-parameter forward is reused.

No truth, labels, groups or ranking targets are arguments to neural inference.
Hyperparameters are an engineering proposal, not fitted on CAL predictions.
"""
from __future__ import annotations
import numpy as np
import torch
from torch.nn import functional as F

VERSION = "mic16_joint_selection_objective_v2_20261008"
GROUP_WEIGHTS = (2.0, 1.0, 1.0, 1.0)
RANKING_WEIGHT = 0.25
RANKING_MARGIN = 1.0


def ranking_mask_from_metadata(metadata, labels, candidate_valid):
    """Build supervised [B,C,C] anchor/negative pairs, never model inputs."""
    y, valid = np.asarray(labels), np.asarray(candidate_valid, bool)
    if y.ndim != 3 or y.shape[-1] != 2 or valid.shape != y.shape[:2] or len(metadata) != len(y):
        raise ValueError("metadata[B], labels[B,C,2], candidate_valid[B,C] required")
    if not np.isin(y, [-1, 0, 1]).all() or not 1 <= y.shape[1] <= 16:
        raise ValueError("labels in {-1,0,1}, 1..16 candidates required")
    pairs = np.zeros((*valid.shape, y.shape[1]), bool)
    for i, entry in enumerate(metadata):
        anchors, sources = [], set()
        for target in entry["ranking_targets"]:
            anchor = target["anchor_candidate"]
            competitors = target["competing_negative_candidates"]
            source = target["source_state_index"]
            if type(anchor) is not int or not 0 <= anchor < y.shape[1] or source in sources:
                raise ValueError("unique valid anchor/source required")
            if anchor in anchors or not valid[i, anchor] or y[i, anchor, 1] != 1:
                raise ValueError("ranking anchor must be a unique valid positive")
            if any(type(j) is not int or not 0 <= j < y.shape[1] for j in competitors) or len(set(competitors)) != len(competitors):
                raise ValueError("unique valid negative indices required")
            if any(not valid[i, j] or y[i, j, 1] != 0 for j in competitors):
                raise ValueError("ranking excludes positives, gray and invalid candidates")
            anchors.append(anchor); sources.add(source)
            pairs[i, anchor, competitors] = True
        if sorted(anchors) != np.flatnonzero(valid[i] & (y[i, :, 1] == 1)).tolist():
            raise ValueError("one metadata anchor per preserved positive required")
    return pairs


def selection_objective_terms(logits, labels, candidate_valid, group_masks, ranking_mask):
    """Weighted group BCE plus per-source mean softplus margin ranking.

    Within each window, available groups have weights (2,1,1,1). Ranking
    averages competitors per nonempty anchor, then anchors per window.
    A window without ranking competitors has zero ranking contribution.
    Finally supervised windows are averaged equally, including zero-source
    windows. Entirely unsupervised windows are omitted from both averages.
    """
    if logits.ndim != 2 or labels.shape != (*logits.shape, 2) or candidate_valid.shape != logits.shape:
        raise ValueError("logits[B,C], labels[B,C,2], candidate_valid[B,C] required")
    b, c = logits.shape
    if b < 1 or not 1 <= c <= 16 or not logits.is_floating_point() or not torch.isfinite(logits).all():
        raise ValueError("finite floating logits and 1..16 candidates required")
    if candidate_valid.dtype != torch.bool or group_masks.dtype != torch.bool or ranking_mask.dtype != torch.bool:
        raise ValueError("valid, group and ranking masks must be boolean")
    if group_masks.shape != (b, 4, c) or ranking_mask.shape != (b, c, c):
        raise ValueError("group_masks[B,4,C], ranking_mask[B,C,C] required")
    if not torch.all((labels == -1) | (labels == 0) | (labels == 1)):
        raise ValueError("labels must be in {-1,0,1}")
    valid = candidate_valid
    supervised = valid & (labels[..., 1] >= 0)
    positive = valid & (labels[..., 1] == 1)
    nuisance = valid & (labels[..., 1] == 0) & (labels[..., 0] == 1)
    if not torch.equal(group_masks.sum(dim=1), supervised.to(torch.int64)):
        raise ValueError("groups must partition supervised candidates")
    if not torch.equal(group_masks[:, 0], positive) or not torch.equal(group_masks[:, 1], nuisance):
        raise ValueError("positive and localized nuisance groups must match labels")
    allowed_pairs = positive.unsqueeze(-1) & (valid & (labels[..., 1] == 0)).unsqueeze(-2)
    if torch.any(ranking_mask & ~allowed_pairs):
        raise ValueError("ranking pairs must join valid positive anchors to selection negatives")
    terms = F.binary_cross_entropy_with_logits(logits, labels[..., 1].clamp_min(0).to(logits.dtype), reduction="none")
    zero = logits.sum() * 0
    bce_windows, rank_windows = [], []
    for i in range(b):
        if not supervised[i].any():
            continue
        numerator, denominator = zero, 0.0
        for group, weight in enumerate(GROUP_WEIGHTS):
            mask = group_masks[i, group]
            if mask.any():
                numerator = numerator + weight * terms[i, mask].mean()
                denominator += weight
        bce_windows.append(numerator / denominator)
        anchors = []
        for anchor in torch.nonzero(ranking_mask[i].any(dim=-1), as_tuple=False).flatten():
            negatives = ranking_mask[i, anchor]
            anchors.append(F.softplus(RANKING_MARGIN + logits[i, negatives] - logits[i, anchor]).mean())
        rank_windows.append(torch.stack(anchors).mean() if anchors else zero)
    bce = torch.stack(bce_windows).mean() if bce_windows else zero
    ranking = torch.stack(rank_windows).mean() if rank_windows else zero
    return dict(total=bce + RANKING_WEIGHT * ranking, weighted_group_bce=bce, source_ranking=ranking)


def selection_objective_loss(logits, labels, candidate_valid, group_masks, ranking_mask):
    return selection_objective_terms(logits, labels, candidate_valid, group_masks, ranking_mask)["total"]
