"""Independent detection and enhancement networks; observations only."""
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from research.candidate_neighborhood_tcn import neighborhood_mask, neighborhood_context
from research.candidate_joint_validity_network import observed_temporal_statistics
from research import directional_tf_mask as tf

VERSION = 'mic16_split_detection_enhancement_v1_20261009'
DET_INPUT, DET_HIDDEN, MASK_HIDDEN = 106, 32, 24


def detection_observations(physical, spectral):
    """20x88 physical observations plus 18 spectral mean/std observations.

    STFT centers are fixed i*16ms; pooling assigns them to the corresponding
    original 2048/39062.5-second frame. Last padded STFT center joins frame19.
    """
    x, s = np.asarray(physical, np.float32), np.asarray(spectral, np.float32)
    assert x.shape[-2:] == (20, 88) and s.shape[-3:] == (67, 64, 10)
    assert x.shape[:-2] == s.shape[:-3]
    frame = np.minimum(np.floor(np.arange(67)*.016/(2048/39062.5)).astype(int), 19)
    features = np.concatenate((s[..., :9].mean(axis=-2), s[..., :9].std(axis=-2)), axis=-1)
    pooled = np.stack([features[..., frame == i, :].mean(axis=-2) for i in range(20)], axis=-2)
    result = np.concatenate((x, pooled), axis=-1).astype(np.float32)
    assert np.isfinite(result).all()
    return result


class DetectionNetwork(nn.Module):
    def __init__(self):
        super().__init__()
        self.project = nn.Linear(DET_INPUT, DET_HIDDEN)
        self.temporal = nn.ModuleList([nn.Conv1d(DET_HIDDEN, DET_HIDDEN, 3, dilation=d) for d in (1, 2, 4)])
        # 自身71维、邻居均值差71维、最大差71维、数量及存在位。
        self.head = nn.Sequential(nn.Linear(215, 64), nn.ReLU(), nn.Linear(64, 32), nn.ReLU(), nn.Linear(32, 1))

    def forward(self, features, directions, valid):
        b, c, t, f = features.shape
        assert (t, f) == (20, DET_INPUT) and valid.shape == (b, c)
        h = F.relu(self.project(features)).reshape(b*c, t, DET_HIDDEN).transpose(1, 2)
        for layer, dilation in zip(self.temporal, (1, 2, 4)):
            h = F.relu(h + layer(F.pad(h, (2*dilation, 0))))
        h = h.reshape(b, c, DET_HIDDEN, t)
        own = torch.cat((h.mean(-1), h.amax(-1), observed_temporal_statistics(features[..., :88])), dim=-1)
        neighbors = neighborhood_mask(directions, valid)
        count = neighbors.sum(-1, keepdim=True); exists = count > 0
        mean = neighbors.to(own.dtype) @ own / count.clamp_min(1)
        maximum = own.unsqueeze(1).expand(-1, c, -1, -1).masked_fill(~neighbors.unsqueeze(-1), -torch.inf).amax(2)
        maximum = torch.where(exists, maximum, torch.zeros_like(maximum))
        context = torch.cat((own, torch.where(exists, own-mean, torch.zeros_like(own)),
            torch.where(exists, own-maximum, torch.zeros_like(own)), count.to(own.dtype)/15, exists.to(own.dtype)), dim=-1)
        return self.head(context).squeeze(-1).masked_fill(~valid, -20.)


class EnhancementBlock(nn.Module):
    def __init__(self, dilation):
        super().__init__(); self.dilation = dilation
        self.depthwise = nn.Conv2d(MASK_HIDDEN, MASK_HIDDEN, 3, groups=MASK_HIDDEN, dilation=(dilation, 1))
        self.pointwise = nn.Conv2d(MASK_HIDDEN, MASK_HIDDEN, 1)

    def forward(self, x):
        return F.relu(x+self.pointwise(F.relu(self.depthwise(F.pad(x, (1, 1, 2*self.dilation, 0))))))


class EnhancementNetwork(nn.Module):
    def __init__(self):
        super().__init__()
        self.project = nn.Conv2d(10, MASK_HIDDEN, 1)
        self.blocks = nn.Sequential(*(EnhancementBlock(d) for d in (1, 2, 4)))
        self.output = nn.Conv2d(MASK_HIDDEN, 1, 1)
        # 初始行为为恒等变换，不随机全局静音。
        nn.init.zeros_(self.output.weight); nn.init.zeros_(self.output.bias)

    def forward(self, features):
        assert features.shape[-2:] == (64, 10)
        shape = features.shape[:-3]
        x = features.reshape(-1, 67, 64, 10).permute(0, 3, 1, 2)
        residual = self.output(self.blocks(F.relu(self.project(x)))).squeeze(1)
        return (1+.5*torch.tanh(residual)).reshape(*shape, 67, 64)


class SplitMultisourceNetwork(nn.Module):
    def __init__(self):
        super().__init__(); self.detector = DetectionNetwork(); self.enhancer = EnhancementNetwork()

    def forward(self, detection_features, spectral_features, directions, valid):
        return dict(validity_logits=self.detector(detection_features, directions, valid),
            masks=self.enhancer(spectral_features))


def enhancement_terms(masks, coefficients, scene_positions):
    """Independent positive-anchor objective; mean anchors then positive windows.

    L=normalized complex spectrum reconstruction + target-component distortion.
    Target preservation coefficient1 is fixed before the run, not CAL-tuned.
    """
    if not len(scene_positions):
        zero = masks.sum()*0
        return dict(total=zero, spectral_reconstruction=zero, target_component_distortion=zero)
    q, r, s = coefficients.unbind(-1)
    reconstruction = (q*masks*masks-2*r*masks+s).sum((-2, -1))
    distortion = ((1-masks)**2*s).sum((-2, -1))
    ids = torch.unique(scene_positions)
    sa = torch.stack([reconstruction[scene_positions == i].mean() for i in ids]).mean()
    fidelity = torch.stack([distortion[scene_positions == i].mean() for i in ids]).mean()
    return dict(total=sa+fidelity, spectral_reconstruction=sa, target_component_distortion=fidelity)


def apply_mask(spectrum, mask, samples):
    m = np.asarray(mask, float)
    assert m.shape == (len(spectrum), 64) and np.isfinite(m).all()
    assert np.all((m >= .5-1e-6) & (m <= 1.5+1e-6))
    full = np.c_[m[:, :1], np.repeat(m, 4, axis=1)]
    return tf.istft(np.asarray(spectrum)*full, samples)


def model_budget(model):
    det = sum(p.numel() for p in model.detector.parameters())
    enh = sum(p.numel() for p in model.enhancer.parameters())
    det_mac = 16*20*(106*32+3*3*32*32)+16*(215*64+64*32+32)
    enh_mac = 67*64*(10*24+3*(9*24+24*24)+24)
    return dict(parameters=det+enh, detection_parameters=det, enhancement_parameters=enh,
        fp32_weight_bytes=4*(det+enh), detection_mac_per_window=det_mac,
        enhancement_mac_per_selected_direction=enh_mac,
        max8_selected_total_mac=det_mac+8*enh_mac,
        excludes='PHAT/physical88/WDAS/STFT/feature reductions/neighborhood/reductions/activations/IO; not FPGA resource or latency proof')
