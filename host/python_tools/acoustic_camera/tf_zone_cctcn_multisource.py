"""Isolated PC candidate: TF-zone spatial CNN + quality + causal TCN.

This is NOT the frozen v1 estimator, nor a deployed FPGA/RTOS model.
Independent sigmoid cells permit zero or several anonymous directions; no
source count, identity, or ground truth is accepted by model.forward().
"""
from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from .geometry_2d import AzElGrid, direction_unit_vector, grid_direction_vectors

CONTRACT_VERSION = "mic16_tf_zone_multilabel_cctcn_candidate_v1"


@dataclass(frozen=True)
class MultisourceCctcnShape:
    zone_count: int = 24
    elevation_bins: int = 13
    azimuth_bins: int = 25
    channels: int = 16
    use_temporal: bool = True
    use_quality: bool = True

    def __post_init__(self):
        if min(self.zone_count, self.elevation_bins, self.azimuth_bins, self.channels) < 1:
            raise ValueError("all dimensions must be positive")

    def to_dict(self):
        return asdict(self)


def build_model(shape: MultisourceCctcnShape = MultisourceCctcnShape()):
    # 延迟导入，Qt导入几何/解码器时不要求PyTorch。
    import torch
    from torch import nn
    from torch.nn import functional as F

    class TfZoneMultisourceCctcn(nn.Module):
        contract_version = CONTRACT_VERSION

        def __init__(self):
            super().__init__()
            self.shape = shape
            c = shape.channels
            self.spatial = nn.Sequential(
                nn.Conv2d(shape.zone_count, c, 3, padding=1), nn.ReLU(),
                nn.Conv2d(c, c, 3, padding=1), nn.ReLU(),
            )
            if shape.use_quality:
                self.quality = nn.Linear(2 * shape.zone_count, 2 * c)
            if shape.use_temporal:
                self.temporal1 = nn.Conv1d(c, c, 3, dilation=1)
                self.temporal2 = nn.Conv1d(c, c, 3, dilation=2)
            self.head = nn.Conv2d(c, 1, 1)
            nn.init.zeros_(self.head.weight)
            nn.init.zeros_(self.head.bias)
            self.physics_scale_raw = nn.Parameter(torch.tensor(4.0))
            self.background_bias = nn.Parameter(torch.tensor(-3.0))

        def forward(self, maps, quality):
            """[B,T,24,13,25], [B,T,48] -> causal [B,T,13,25] logits.

            Time pooling is a PREFIX mean, never a whole-sequence mean.
            The final frame describes all evidence observed in that window;
            outputs are event-window occupancy, not speaker-frame VAD.
            """
            s = self.shape
            if maps.ndim != 5 or tuple(maps.shape[2:]) != (
                s.zone_count, s.elevation_bins, s.azimuth_bins
            ) or maps.shape[0] < 1 or maps.shape[1] < 1:
                raise ValueError("maps must have shape [B,T,Z,E,A], B,T positive")
            b, t, z, e, a = maps.shape
            if tuple(quality.shape) != (b, t, 2 * z):
                raise ValueError("quality must have shape [B,T,2*Z]")
            if not torch.isfinite(maps).all() or not torch.isfinite(quality).all():
                raise ValueError("inputs must be finite")
            if maps.device != quality.device or maps.dtype != quality.dtype:
                raise ValueError("maps and quality must share dtype and device")
            spatial = self.spatial(maps.reshape(b * t, z, e, a))
            if s.use_quality:
                gain, bias = self.quality(quality.reshape(b * t, 2 * z)).chunk(2, dim=1)
                spatial = F.relu(spatial * torch.sigmoid(gain)[:, :, None, None]
                                 + 0.1 * torch.tanh(bias)[:, :, None, None])
            features = spatial.reshape(b, t, s.channels, e, a)
            if s.use_temporal:
                temporal = features.permute(0, 3, 4, 2, 1).reshape(b * e * a, s.channels, t)
                # 只在左侧补零，不使用批归一化或全局时间统计。
                temporal = temporal + F.relu(self.temporal1(F.pad(temporal, (2, 0))))
                temporal = temporal + F.relu(self.temporal2(F.pad(temporal, (4, 0))))
                features = temporal.reshape(b, e, a, s.channels, t).permute(0, 4, 3, 1, 2)
            denominator = torch.arange(1, t + 1, device=maps.device, dtype=maps.dtype)
            pooled = features.cumsum(dim=1) / denominator[None, :, None, None, None]
            learned = self.head(pooled.reshape(b * t, s.channels, e, a)).reshape(b, t, e, a)
            if s.use_quality:
                # 质量前Z维为超额dB除20，不含声源标签。
                weights = torch.sigmoid(quality[:, :, :z] * 2.0)
            else:
                weights = torch.ones_like(quality[:, :, :z])
            physics = (maps * weights[:, :, :, None, None]).sum(2)
            physics = physics / weights.sum(2).clamp_min(1e-6)[:, :, None, None]
            physics = physics.cumsum(1) / denominator[None, :, None, None]
            return learned + F.softplus(self.physics_scale_raw) * physics + self.background_bias

    return TfZoneMultisourceCctcn()


def model_budget(shape: MultisourceCctcnShape = MultisourceCctcnShape(), frames: int = 20):
    """Analytic dense MAC count; not an ARM/FPGA latency measurement."""
    if frames < 1:
        raise ValueError("frames must be positive")
    z, c, g = shape.zone_count, shape.channels, shape.elevation_bins * shape.azimuth_bins
    spatial_params = z * c * 9 + c + c * c * 9 + c
    quality_params = (2 * z * 2 * c + 2 * c) if shape.use_quality else 0
    temporal_params = 2 * (c * c * 3 + c) if shape.use_temporal else 0
    params = spatial_params + quality_params + temporal_params + c + 1 + 2
    macs = frames * (g * (z * c * 9 + c * c * 9 + c)
                     + (2 * z * 2 * c if shape.use_quality else 0)
                     + (g * 2 * c * c * 3 if shape.use_temporal else 0))
    return {"parameters": params, "dense_macs_per_window": macs,
            "float32_weights_bytes": params * 4, "frames": frames,
            "includes_phat_frontend": False, "latency_measured": False}


def make_target(truth_directions, grid: AzElGrid = AzElGrid(), sigma_deg: float = 5.0):
    """Spherical Gaussian union. Used by TRAIN/evaluation only, never inference."""
    if not np.isfinite(sigma_deg) or sigma_deg <= 0:
        raise ValueError("sigma_deg must be positive and finite")
    target = np.zeros(grid.shape, np.float32)
    vectors = grid_direction_vectors(grid).astype(np.float64)
    vectors /= np.linalg.norm(vectors, axis=-1, keepdims=True)
    for azimuth, elevation in truth_directions:
        if not np.isfinite([azimuth, elevation]).all() or not (
            grid.azimuth_min_deg <= azimuth <= grid.azimuth_max_deg
            and grid.elevation_min_deg <= elevation <= grid.elevation_max_deg
        ):
            raise ValueError("truth outside the declared grid")
        vector = direction_unit_vector(azimuth, elevation).astype(np.float64)
        vector /= np.linalg.norm(vector)
        distance = np.rad2deg(np.arccos(np.clip(vectors @ vector, -1, 1)))
        target = np.maximum(target, np.exp(-0.5 * (distance / sigma_deg) ** 2))
    return target.astype(np.float32)


def decode_directions(probability, threshold: float, grid: AzElGrid = AzElGrid(),
                      nms_deg: float = 15.0, max_outputs: int = 8):
    """Threshold + local maxima + spherical NMS. Does NOT accept oracle K."""
    from scipy.ndimage import maximum_filter
    from .geometry_2d import great_circle_error_deg

    values = np.asarray(probability, dtype=np.float64)
    if values.shape != grid.shape or not np.isfinite(values).all():
        raise ValueError("probability must be a finite [E,A] grid")
    if np.any(values < 0) or np.any(values > 1):
        raise ValueError("probability values must be in [0,1]")
    if not np.isfinite(threshold) or not 0 < threshold < 1:
        raise ValueError("threshold must be inside (0,1)")
    if not np.isfinite(nms_deg) or nms_deg <= 0 or max_outputs < 1:
        raise ValueError("invalid decoder budget/radius")
    if np.ptp(values) <= 1e-12:
        return []  # 平坦场不包含可识别的局部方向。
    maxima = maximum_filter(values, size=3, mode="constant", cval=-1)
    indices = np.flatnonzero((values >= threshold) & (values == maxima))
    ranked = sorted(indices, key=lambda i: (-values.flat[i], int(i)))
    outputs = []
    for index in ranked:
        ei, ai = np.unravel_index(index, grid.shape)
        direction = (float(grid.azimuths_deg[ai]), float(grid.elevations_deg[ei]))
        if all(great_circle_error_deg(*direction, *old) >= nms_deg for old in outputs):
            outputs.append(direction)
        if len(outputs) == max_outputs:
            break
    return outputs


def match_directions(predictions, truths, match_deg: float = 10.0):
    """One-to-one maximum-cardinality matching, then minimum spherical error."""
    from scipy.optimize import linear_sum_assignment
    from .geometry_2d import great_circle_error_deg

    if not np.isfinite(match_deg) or match_deg <= 0 or match_deg > 180:
        raise ValueError("invalid matching radius")
    p, k = len(predictions), len(truths)
    if p == 0 or k == 0:
        return {"tp": 0, "fp": p, "fn": k, "errors_deg": []}
    error = np.asarray([[great_circle_error_deg(*pred, *truth) for truth in truths]
                        for pred in predictions])
    # 虚拟节点允许任一侧不匹配，有效配对节省2000代价；
    # 无效配对不可选，因此先最大化匹配数，再最小化角误差。
    cost = np.zeros((p + k, p + k))
    cost[:p, :k] = np.where(error <= match_deg, error, 1e9)
    cost[:p, k:] = 1000
    cost[p:, :k] = 1000
    row, col = linear_sum_assignment(cost)
    matched = [float(error[r, c]) for r, c in zip(row, col)
               if r < p and c < k and error[r, c] <= match_deg]
    return {"tp": len(matched), "fp": p - len(matched), "fn": k - len(matched),
            "errors_deg": matched}
