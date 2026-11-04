"""Small observed-only azimuth/elevation/time prototype, not a trained release.

The physical projector and independent background reference remain unchanged.
Counts supervise an auxiliary head; they never force the map decoder to emit K.
"""
from __future__ import annotations
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from acoustic_camera.tf_zone_histogram_2d import TfZoneProjector
from acoustic_camera.tf_zone_neural_features import extract_neural_features
from acoustic_camera.valid_source_detection import angular_distance_deg
from research.all_source_detection import candidate_targets, all_source_loss

VERSION = 'ordinary_spatial_spectrum_joint_prototype_20261013'
INDICES = [0, 4, 10, 13, 48, 52, 58, 61]
SEED, THRESHOLD, CHANNELS = 20261012, .65, 6


class SpatialSpectrumFeatures:
    def __init__(self):
        self.projector = TfZoneProjector()
        az, el = np.meshgrid(self.projector.grid.azimuths_deg,
                             self.projector.grid.elevations_deg)
        self.directions = np.c_[az.ravel(), el.ravel()].astype(np.float32)
        assert az.shape == (13, 25) and self.directions.shape == (325, 2)

    def extract(self, frames, reference):
        a, n = np.asarray(frames), np.asarray(reference)
        if a.shape != (20, 2048, 16) or n.shape != (4, 2048, 16):
            raise ValueError('complete20-frame audio and independent4-frame reference required')
        if not all(np.issubdtype(x.dtype, np.floating) for x in (a, n)):
            raise ValueError('decoded floating PCM required')
        if any(not np.isfinite(x).all() or np.max(np.abs(x)) > 1 for x in (a, n)):
            raise ValueError('finite normalized PCM required')
        maps, quality = extract_neural_features(self.projector, a, n)
        # 固定裁剪和缩放属于原提取器，不按窗做峰值
        # 归一化，不接收真值权重、语义或真实源数。
        excess = np.broadcast_to(quality[:, :24, None, None], maps.shape)
        features = np.concatenate((maps, excess), axis=1).astype(np.float32)
        assert features.shape == (20, 48, 13, 25) and np.isfinite(features).all()
        return dict(features=features, maps=maps, quality=quality)


class SpatialBlock(nn.Module):
    def __init__(self, channels=CHANNELS):
        super().__init__()
        self.depthwise = nn.Conv2d(channels, channels, 3, padding=1, groups=channels)
        self.pointwise = nn.Conv2d(channels, channels, 1)

    def forward(self, x):
        return F.relu(x + self.pointwise(F.relu(self.depthwise(x))))


class TemporalBlock(nn.Module):
    def __init__(self, dilation, channels=CHANNELS):
        super().__init__()
        self.dilation = dilation
        self.depthwise = nn.Conv1d(channels, channels, 3, dilation=dilation, groups=channels)
        self.pointwise = nn.Conv1d(channels, channels, 1)

    def forward(self, x):
        return F.relu(x + self.pointwise(F.relu(self.depthwise(F.pad(x, (2*self.dilation, 0))))))


class SpatialSpectrumJoint(nn.Module):
    def __init__(self):
        super().__init__()
        self.in_projection = nn.Conv2d(48, CHANNELS, 1)
        self.spatial = nn.ModuleList([SpatialBlock(), SpatialBlock()])
        self.temporal = nn.ModuleList([TemporalBlock(d) for d in (1, 2, 4)])
        self.map_head = nn.Conv2d(2*CHANNELS, 1, 1)
        self.count_head = nn.Sequential(nn.Linear(4*CHANNELS, 12), nn.ReLU(), nn.Linear(12, 3))

    def encode(self, x):
        if x.ndim != 5 or tuple(x.shape[1:]) != (20, 48, 13, 25) or not torch.isfinite(x).all():
            raise ValueError('finite input[B,T=20,C=48,E=13,A=25] required')
        b, t, c, e, a = x.shape
        y = F.relu(self.in_projection(x.reshape(b*t, c, e, a)))
        for block in self.spatial:
            y = block(y)
        # 每个角度单元是独立时间流，不混合坐标轴。
        y = y.reshape(b, t, CHANNELS, e, a).permute(0, 3, 4, 2, 1).reshape(b*e*a, CHANNELS, t)
        for block in self.temporal:
            y = block(y)
        return y.reshape(b, e, a, CHANNELS, t).permute(0, 4, 3, 1, 2)

    def forward(self, x):
        y = self.encode(x)
        pooled = torch.cat((y.mean(dim=1), y.amax(dim=1)), dim=1)
        map_logits = self.map_head(pooled)[:, 0]
        global_features = torch.cat((pooled.mean(dim=(2, 3)), pooled.amax(dim=(2, 3))), dim=1)
        return dict(map_logits=map_logits, count_logits=self.count_head(global_features))


def map_targets(directions, record):
    target = candidate_targets(directions, np.ones(325, bool), record)
    if target['uncovered'] or target['gray'] or len(target['truth']) > 2:
        raise ValueError('prototype requires covered ordinary0..2 active points, no gray sources')
    return dict(**target, map_labels=target['labels'].reshape(13, 25), count_label=len(target['truth']))


def joint_loss(output, map_labels, count_labels):
    z = output['map_logits']; c = output['count_logits']
    if map_labels.shape != z.shape or c.shape != (len(z), 3) or count_labels.shape != (len(z),):
        raise ValueError('matching map and count label shapes required')
    if not torch.isfinite(c).all() or count_labels.dtype != torch.long:
        raise ValueError('finite count logits and integer count supervision required')
    map_loss = all_source_loss(z.flatten(1), map_labels.flatten(1), torch.ones_like(map_labels, dtype=torch.bool).flatten(1))
    count_loss = F.cross_entropy(c, count_labels)
    # 工程参数在拟合前固定，不冒称复制论文超参数。
    return .5*map_loss + .5*count_loss, dict(map_BCE=map_loss, count_CE=count_loss)


def decode_map(map_logits, count_logits, directions):
    z, c, d = np.asarray(map_logits, float), np.asarray(count_logits, float), np.asarray(directions)
    if z.shape != (13, 25) or c.shape != (3,) or d.shape != (325, 2) or not np.isfinite(z).all() or not np.isfinite(c).all():
        raise ValueError('finite map[13,25], count[3], directions[325,2] required')
    score = 1/(1+np.exp(-np.clip(z, -700, 700)))
    padded = np.pad(score, 1, constant_values=-np.inf)
    neighborhood = np.lib.stride_tricks.sliding_window_view(padded, (3, 3)).max(axis=(-1, -2))
    ids = np.flatnonzero(((score >= THRESHOLD) & (score == neighborhood)).ravel())
    ids = sorted(ids.tolist(), key=lambda i: (-score.ravel()[i], i))
    selected = []
    for i in ids:
        if all(angular_distance_deg(*d[i], *d[j]) > 10 for j in selected):
            selected.append(i)
            if len(selected) == 8:
                break
    sources = [dict(source_id=j+1, azimuth_deg=float(d[i, 0]), elevation_deg=float(d[i, 1]),
                    detection_score=float(score.ravel()[i]), grid_index=i) for j, i in enumerate(selected)]
    auxiliary_count = int(np.argmax(c))
    return dict(sources=sources, source_count=len(sources), auxiliary_count=auxiliary_count,
                count_agreement=auxiliary_count == len(sources), threshold=THRESHOLD,
                true_count_used=False, predicted_count_forces_TopK=False)


def architecture_budget(model):
    params = sum(p.numel() for p in model.parameters())
    terms = dict(input_projection=20*325*48*6, spatial_blocks=2*20*325*(6*9+6*6),
                 causal_temporal_blocks=3*20*325*(6*3+6*6), map_head=325*12,
                 count_head=24*12+12*3)
    assert params == 1048
    return dict(parameters=params, fp32_weight_bytes=4*params, dense_MAC_per_window=sum(terms.values()),
                MAC_terms=terms, input_shape=[20,48,13,25], input_fp32_bytes=20*48*325*4,
                encoder_output_fp32_bytes=20*6*325*4, raw_map_fp32_bytes=20*24*325*4,
                observation_s=40960/39062.5, temporal_receptive_field_frames=15,
                excludes='FFT/PHAT/reference extraction/activations/additions/ReLU/pooling/decoder/IO; not FPGA cost or peak RAM',
                final_window_pooling_waits_for_complete_window=True)


def numpy_forward(model, x):
    """Independent NumPy convolution/axis replay, no Torch forward call."""
    w = {k: v.detach().cpu().numpy() for k,v in model.state_dict().items()}
    x = np.asarray(x, np.float32); b,t,c,e,a = x.shape
    relu = lambda y: np.maximum(y, np.float32(0))
    def point(y, name):
        out = np.einsum('oc,bc...->bo...', w[name+'.weight'].reshape(len(w[name+'.bias']), -1), y, optimize=True)
        return out + w[name+'.bias'].reshape((1,-1)+(1,)*(y.ndim-2))
    y = relu(point(x.reshape(b*t,c,e,a), 'in_projection'))
    for k in range(2):
        name=f'spatial.{k}'
        patches=np.lib.stride_tricks.sliding_window_view(np.pad(y, ((0,0),(0,0),(1,1),(1,1))), (3,3), axis=(-2,-1))
        depth=np.einsum('bceaxy,cxy->bcea', patches, w[name+'.depthwise.weight'][:,0], optimize=True)
        depth += w[name+'.depthwise.bias'][None,:,None,None]
        y=relu(y+point(relu(depth), name+'.pointwise'))
    y=y.reshape(b,t,6,e,a).transpose(0,3,4,2,1).reshape(b*e*a,6,t)
    for k,dilation in enumerate((1,2,4)):
        name=f'temporal.{k}'; padded=np.pad(y, ((0,0),(0,0),(2*dilation,0)))
        depth=sum(padded[:,:,j*dilation:j*dilation+t]*w[name+'.depthwise.weight'][:,0,j][None,:,None] for j in range(3))
        depth += w[name+'.depthwise.bias'][None,:,None]
        y=relu(y+point(relu(depth), name+'.pointwise'))
    y=y.reshape(b,e,a,6,t).transpose(0,4,3,1,2)
    pooled=np.concatenate((y.mean(axis=1),y.max(axis=1)),axis=1)
    map_logits=point(pooled,'map_head')[:,0]
    g=np.concatenate((pooled.mean(axis=(2,3)),pooled.max(axis=(2,3))),axis=1)
    g=relu(g@w['count_head.0.weight'].T+w['count_head.0.bias'])
    return dict(map_logits=map_logits, count_logits=g@w['count_head.2.weight'].T+w['count_head.2.bias'])
