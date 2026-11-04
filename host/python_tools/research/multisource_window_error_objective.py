"""Training-only aggregate false-alarm/miss penalty; inference is unchanged."""
import torch
from research import multisource_hard_negative_objective as prior

VERSION = 'mic16_window_soft_error_mass_v1_20261009'
ERROR_MASS_WEIGHT = .25


def terms(logits, labels, valid, groups, ranking_pairs):
    original = prior.terms(logits, labels, valid, groups, ranking_pairs)
    supervised = valid & (labels[..., 1] >= 0)
    positive = valid & (labels[..., 1] == 1)
    negative = valid & (labels[..., 1] == 0)
    probability = torch.sigmoid(logits)
    # 在窗内累加监督机会，新增负例不能在组均值
    # 或旧前三项归约之外消失；误报与漏检不能像
    # 预测源数减真实源数那样互相抵消。
    false_mass = (probability * negative).sum(-1)
    miss_mass = ((1-probability) * positive).sum(-1)
    windows = supervised.any(-1)
    zero = logits.sum()*0
    false = false_mass[windows].mean() if windows.any() else zero
    miss = miss_mass[windows].mean() if windows.any() else zero
    return dict(total=original['total']+ERROR_MASS_WEIGHT*(false+miss),
                hard_group_bce=original['hard_group_bce'], source_ranking=original['source_ranking'],
                soft_false_alarm_mass=false, soft_miss_mass=miss)
