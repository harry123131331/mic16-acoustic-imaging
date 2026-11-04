"""Untrained neighborhood scorer; separate from all sealed checkpoints.

Only observed candidate directions, features and padding validity enter forward.
The old temporal encoder is reused; local mean/max contrasts make neighboring
candidates visible to the score head. This is a hypothesis, not a new result.
"""
from __future__ import annotations
import math
import torch
from torch import nn
from torch.nn import functional as F
from research.candidate_joint_validity_network import (
    CandidateJointValidityTCN, observed_temporal_statistics, CAP, HIDDEN, FEATURES, FRAMES)

VERSION = "mic16_candidate_neighborhood_tcn_4161_draft_20261008"
RADIUS_DEG = 15.0
POOLED, CONTEXT = 23, 71


def neighborhood_mask(directions, valid):
    """Spherical neighborhood, excluding self and padding; no true DOA/K."""
    if directions.ndim != 3 or directions.shape[-1] != 2 or valid.shape != directions.shape[:2]:
        raise ValueError("observed directions[B,C,2], valid[B,C] required")
    if not 1 <= directions.shape[1] <= CAP or not torch.isfinite(directions).all():
        raise ValueError("finite observed directions and 1..16 candidates required")
    radians = directions * (math.pi / 180)
    azimuth, elevation = radians.unbind(dim=-1)
    unit = torch.stack((elevation.cos()*azimuth.sin(), elevation.sin(),
                        elevation.cos()*azimuth.cos()), dim=-1)
    dot = unit @ unit.transpose(-1, -2)
    mask = dot >= math.cos(math.radians(RADIUS_DEG))
    mask &= valid.bool().unsqueeze(-1) & valid.bool().unsqueeze(-2)
    mask &= ~torch.eye(directions.shape[1], dtype=torch.bool, device=directions.device).unsqueeze(0)
    return mask


def neighborhood_context(pooled, mask):
    """Own 23 + mean contrast 23 + max contrast 23 + count/15 + exists.

    No-neighbor contrast is zero. Mean and maximum are permutation invariant
    over the observed neighbor set. Invalid candidates cannot affect others.
    """
    if pooled.ndim != 3 or pooled.shape[-1] != POOLED or mask.shape != (*pooled.shape[:2], pooled.shape[1]):
        raise ValueError("pooled[B,C,23], mask[B,C,C] required")
    count = mask.sum(dim=-1, keepdim=True)
    exists = count > 0
    mean = mask.to(pooled.dtype) @ pooled / count.clamp_min(1)
    neighbors = pooled.unsqueeze(1).expand(-1, pooled.shape[1], -1, -1)
    maximum = neighbors.masked_fill(~mask.unsqueeze(-1), -torch.inf).amax(dim=2)
    maximum = torch.where(exists, maximum, torch.zeros_like(maximum))
    mean_contrast = torch.where(exists, pooled - mean, torch.zeros_like(pooled))
    max_contrast = torch.where(exists, pooled - maximum, torch.zeros_like(pooled))
    return torch.cat((pooled, mean_contrast, max_contrast,
                      count.to(pooled.dtype)/(CAP-1), exists.to(pooled.dtype)), dim=-1)


class CandidateNeighborhoodTCN(CandidateJointValidityTCN):
    def __init__(self):
        super().__init__()
        self.validity_hidden = nn.Linear(CONTEXT, HIDDEN)

    def forward(self, features, directions, candidate_valid):
        if features.shape[:2] != directions.shape[:2] or features.device != directions.device:
            raise ValueError("matching feature/direction batches and devices required")
        mask = neighborhood_mask(directions, candidate_valid)
        sequence = self.temporal_features(features)
        pooled = torch.cat((sequence.mean(dim=2), observed_temporal_statistics(features)), dim=-1)
        context = neighborhood_context(pooled, mask)
        return self.validity_output(F.relu(self.validity_hidden(context))).squeeze(-1)


def model_budget(model):
    parameters = sum(p.numel() for p in model.parameters())
    assert parameters == 4161
    return dict(parameters=parameters, fp32_weight_bytes=4*parameters,
        dense_network_mac_per_window=CAP*FRAMES*(FEATURES*HIDDEN+2*HIDDEN*HIDDEN*3)+CAP*(CONTEXT*HIDDEN+HIDDEN),
        extra_dense_mac_vs_3393=CAP*(CONTEXT-POOLED)*HIDDEN,
        context_channels=CONTEXT, neighborhood_radius_deg=RADIUS_DEG,
        max_directed_neighbor_edges=CAP*(CAP-1),
        observation_seconds=40960/39062.5,
        excludes="FFT/PHAT/DAS/statistics/geometry/mean/max/activations/IO; not measured FPGA latency")
