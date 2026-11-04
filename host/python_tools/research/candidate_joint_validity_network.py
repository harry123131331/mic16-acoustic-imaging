"""Untrained candidate neural main-chain design, separate from sealed v1 runs.

The network predicts correctly localized active speech for every candidate.
Spatial labels only define training loss groups; no labels/identity/K enter
the forward pass. A disabled-chain fallback is an explicit separate action.
"""
from __future__ import annotations
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION = "mic16_candidate_joint_validity_tcn_3393_v1_20261008"
FRAMES, FEATURES, HIDDEN, CAP = 20, 88, 16, 16
STATISTICS = (
    "beam_energy_temporal_std_div20", "mean_band_temporal_std_div40",
    "mean_band_temporal_absdiff_div40", "coarse_band_entropy_mean",
    "coarse_band_entropy_temporal_std", "coarse_band_flatness_mean",
    "coarse_band_flatness_temporal_std",
)


def observed_temporal_statistics(features):
    """Seven whole-window quantities from the existing clipped beam inputs.

    Band entropy/flatness use 32 equal-width band-mean powers. They do not
    reproduce the old FFT-bin event metrics. std uses population definition.
    This whole-window operator retains the original 1.048576 s observation.
    """
    if features.ndim != 4 or tuple(features.shape[-2:]) != (FRAMES, FEATURES):
        raise ValueError("features require [B,C,20,88]")
    bands = features[..., 48:80]
    log_power = bands * (40*np.log(10)/10)
    log_probability = log_power - torch.logsumexp(log_power, dim=-1, keepdim=True)
    probability = torch.exp(log_probability)
    entropy = -(probability*log_probability).sum(dim=-1)/np.log(32)
    flatness = torch.exp(log_power.mean(dim=-1) - torch.logsumexp(log_power, dim=-1) + np.log(32))
    return torch.stack((features[..., 86].std(dim=-1, correction=0),
        bands.std(dim=-2, correction=0).mean(dim=-1),
        (bands[..., 1:, :] - bands[..., :-1, :]).abs().mean(dim=(-2, -1)),
        entropy.mean(dim=-1), entropy.std(dim=-1, correction=0),
        flatness.mean(dim=-1), flatness.std(dim=-1, correction=0)), dim=-1)


class CandidateJointValidityTCN(nn.Module):
    def __init__(self):
        super().__init__()
        self.projection = nn.Linear(FEATURES, HIDDEN)
        self.temporal1 = nn.Conv1d(HIDDEN, HIDDEN, 3, dilation=1)
        self.temporal2 = nn.Conv1d(HIDDEN, HIDDEN, 3, dilation=2)
        self.validity_hidden = nn.Linear(HIDDEN+len(STATISTICS), HIDDEN)
        self.validity_output = nn.Linear(HIDDEN, 1)

    def temporal_features(self, features):
        if features.ndim != 4 or tuple(features.shape[-2:]) != (FRAMES, FEATURES):
            raise ValueError("features require [B,C,20,88]")
        b, c = features.shape[:2]
        if b < 1 or not 1 <= c <= CAP or not features.is_floating_point() or not torch.isfinite(features).all():
            raise ValueError("finite floating features and 1..16 candidates required")
        x = F.relu(self.projection(features)).reshape(b*c, FRAMES, HIDDEN).transpose(1, 2)
        x = F.relu(self.temporal1(F.pad(x, (2, 0))))
        x = F.relu(self.temporal2(F.pad(x, (4, 0))))
        return x.transpose(1, 2).reshape(b, c, FRAMES, HIDDEN)

    def forward(self, features):
        sequence = self.temporal_features(features)
        pooled = torch.cat((sequence.mean(dim=2), observed_temporal_statistics(features)), dim=-1)
        return self.validity_output(F.relu(self.validity_hidden(pooled))).squeeze(-1)


def joint_validity_groups(labels, candidate_valid):
    """Supervision groups: speech positive, localized nuisance, other negative.

    Missing groups are omitted. -1 speech targets and invalid candidates are
    excluded regardless of their spatial labels. No group is a forward input.
    """
    if labels.ndim != 3 or labels.shape[-1] != 2 or candidate_valid.shape != labels.shape[:2]:
        raise ValueError("labels[B,C,2], candidate_valid[B,C] required")
    if not torch.all((labels == -1) | (labels == 0) | (labels == 1)):
        raise ValueError("labels must be in {-1,0,1}")
    valid = candidate_valid.bool()
    positive = valid & (labels[..., 1] == 1)
    nuisance = valid & (labels[..., 1] == 0) & (labels[..., 0] == 1)
    other = valid & (labels[..., 1] == 0) & (labels[..., 0] != 1)
    return positive, nuisance, other


def joint_validity_loss(logits, labels, candidate_valid):
    if logits.shape != labels.shape[:2] or not torch.isfinite(logits).all():
        raise ValueError("finite logits[B,C] required")
    masks = joint_validity_groups(labels, candidate_valid)
    terms = F.binary_cross_entropy_with_logits(logits, labels[..., 1].clamp_min(0).to(logits.dtype), reduction="none")
    windows = []
    for i in range(len(logits)):
        groups = [terms[i][mask[i]].mean() for mask in masks if mask[i].any()]
        if groups:
            windows.append(torch.stack(groups).mean())
    return torch.stack(windows).mean() if windows else logits.sum()*0


def decode_joint_validity(directions, probabilities, candidate_valid, threshold):
    """Decode every candidate; there is no permanently accepted physical list.

    Score descending, original-index ties, 10-degree duplicate exclusion and
    at most eight outputs. Scores are uncalibrated, not correctness probabilities.
    """
    d, p, valid = np.asarray(directions, float), np.asarray(probabilities, float), np.asarray(candidate_valid, bool)
    if d.ndim != 2 or d.shape[1] != 2 or p.shape != (len(d),) or valid.shape != p.shape or not 1 <= len(d) <= CAP:
        raise ValueError("directions[1..16,2], probabilities[C], valid[C] required")
    if not np.isfinite(d).all() or not np.isfinite(p).all() or not np.isfinite(threshold) or not 0 <= threshold <= 1 or np.any((p < 0) | (p > 1)):
        raise ValueError("finite directions and probabilities/threshold in [0,1] required")
    outputs, selected = [], []
    for i in sorted((j for j in range(len(d)) if valid[j] and p[j] >= threshold), key=lambda j: (-p[j], j)):
        if len(outputs) == 8:
            break
        if any(angular_distance_deg(*d[i], *point) <= 10 for point in outputs):
            continue
        outputs.append(d[i].tolist())
        selected.append(i)
    return outputs, selected


def choose_main_chain_outputs(physical_reference, directions, probabilities, candidate_valid, threshold, enabled=True):
    """Explicit disabled-chain fallback; normal inference ignores reference.

    A low neural score does not silently activate the physical reference.
    Invalid inputs raise; a future caller must expose any fault fallback mode.
    """
    if not enabled:
        return [list(point) for point in physical_reference], []
    return decode_joint_validity(directions, probabilities, candidate_valid, threshold)


def model_budget(model):
    layers = ("projection", "temporal1", "temporal2", "validity_hidden", "validity_output")
    parts = {name: sum(p.numel() for p in getattr(model, name).parameters()) for name in layers}
    parameters = sum(parts.values())
    return dict(parameters=parameters, per_layer_parameters=parts, fp32_weight_bytes=4*parameters,
        dense_network_mac_per_window=CAP*FRAMES*(FEATURES*HIDDEN+2*HIDDEN*HIDDEN*3)+CAP*((HIDDEN+len(STATISTICS))*HIDDEN+HIDDEN),
        sequence_encoder_receptive_field_frames=7, statistics_window_frames=20,
        full_window_wait_seconds=40960/39062.5,
        excludes="PHAT/DAS/FFT/temporal statistics/log/exp/activation/copy/IO; MAC is not board timing")
