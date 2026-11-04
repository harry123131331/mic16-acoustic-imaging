"""Training-only hard-negative reduction; inference and labels unchanged."""
import torch
from torch.nn import functional as F
from research.candidate_joint_selection_objective_v2 import selection_objective_terms

VERSION = 'mic16_detector_group_top3_negative_v1_20261009'
GROUP_WEIGHTS = (3., 1., 1., 1.)
NEGATIVE_CAP = 3
RANKING_WEIGHT = .25
GROUP_NAMES = ('positive_anchor', 'localized_nuisance_negative', 'near_ON_speech_negative', 'other_negative')


def terms(logits, labels, valid, groups, ranking_pairs):
    # 复用既有标签/分组划分及正负例验证。
    original = selection_objective_terms(logits, labels, valid, groups, ranking_pairs)
    element = F.binary_cross_entropy_with_logits(logits, labels[..., 1].clamp_min(0).to(logits.dtype), reduction='none')
    windows = []
    for i in range(len(logits)):
        numerator = logits.sum()*0; denominator = 0.
        for g, weight in enumerate(GROUP_WEIGHTS):
            values = element[i, groups[i, g]]
            if len(values):
                if g:
                    values = torch.topk(values, min(NEGATIVE_CAP, len(values)), sorted=False).values
                numerator = numerator+weight*values.mean(); denominator += weight
        if denominator:
            windows.append(numerator/denominator)
    bce = torch.stack(windows).mean() if windows else logits.sum()*0
    return dict(total=bce+RANKING_WEIGHT*original['source_ranking'], hard_group_bce=bce,
        source_ranking=original['source_ranking'])
