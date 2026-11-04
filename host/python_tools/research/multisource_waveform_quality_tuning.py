"""Same split architecture, waveform quality objective, no truth in inference."""
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from research.multisource_split_network import DetectionNetwork, EnhancementNetwork, model_budget, detection_observations
from research.candidate_joint_selection_objective_v2 import selection_objective_terms

VERSION = 'mic16_split_waveform_quality_tuning_v2_20261009'
SAMPLES = 16778
GROUP_WEIGHTS = (3.,1.,1.,1.)


class ConservativeEnhancementNetwork(EnhancementNetwork):
    def forward(self,features):
        return 1+.4*(super().forward(features)-1)


class WaveformQualityNetwork(nn.Module):
    def __init__(self):
        super().__init__()
        self.detector = DetectionNetwork()
        self.enhancer = ConservativeEnhancementNetwork()

    def forward(self,detection_features,spectral_features,directions,valid):
        return dict(validity_logits=self.detector(detection_features,directions,valid),
            masks=self.enhancer(spectral_features))


def torch_istft(spectra,masks,samples=SAMPLES):
    """Exactly the PC periodic-Hann, hop256, padded OLA; differentiable."""
    if spectra.shape != (*masks.shape[:-1],257) or masks.shape[-2:] != (67,64):
        raise ValueError('spectra[N,67,257], masks[N,67,64] required')
    full = torch.cat((masks[:,:,:1],masks.repeat_interleave(4,dim=-1)),dim=-1)
    window = torch.hann_window(512,periodic=True,dtype=masks.dtype,device=masks.device)
    blocks = torch.fft.irfft(spectra*full,n=512,dim=-1)*window
    length = (67-1)*256+512
    signal = F.fold(blocks.transpose(1,2),output_size=(1,length),kernel_size=(1,512),stride=(1,256)).flatten(1)
    norm = F.fold(window.square()[None,:,None].expand(1,-1,67),output_size=(1,length),kernel_size=(1,512),stride=(1,256)).flatten(1)
    return (signal/norm.clamp_min(1e-8))[:,256:256+samples]


def crop_mask(guards,samples=SAMPLES):
    return (torch.arange(samples,device=guards.device)[None,:] >= guards[:,None]).to(torch.float32)


def sisdr(estimate,reference,guards):
    valid = crop_mask(guards,estimate.shape[-1]).to(estimate.dtype)
    count = valid.sum(-1,keepdim=True)
    x = (estimate-(estimate*valid).sum(-1,keepdim=True)/count)*valid
    s = (reference-(reference*valid).sum(-1,keepdim=True)/count)*valid
    target_power = s.square().sum(-1).clamp_min(1e-20)
    dot = (x*s).sum(-1)
    projection = dot.square()/target_power
    residual = (x-dot[:,None]/target_power[:,None]*s).square().sum(-1)
    epsilon = target_power*1e-10
    return 10*torch.log10(projection.clamp_min(epsilon)/residual.clamp_min(epsilon))


def window_average(values,positions):
    if not len(positions):
        return values.sum()*0
    return torch.stack([values[positions == i].mean() for i in torch.unique(positions)]).mean()


def waveform_terms(masks,mixture_spectra,target_spectra,reference,guards,positions):
    if not len(positions):
        zero = masks.sum()*0
        return dict(total=zero,quality=zero,target_distortion=zero,mean_SI_SDR_gain_db=zero,mean_target_power_transfer_db=zero)
    ones = torch.ones_like(masks)
    y = torch_istft(mixture_spectra,masks)
    baseline = torch_istft(mixture_spectra,ones)
    target = torch_istft(target_spectra,ones)
    masked_target = torch_istft(target_spectra,masks)
    valid = crop_mask(guards).to(masks.dtype)
    gain = sisdr(y,reference,guards)-sisdr(baseline,reference,guards)
    target_energy = (target.square()*valid).sum(-1).clamp_min(1e-20)
    distortion = ((masked_target-target).square()*valid).sum(-1)/target_energy
    transfer = 10*torch.log10((masked_target.square()*valid).sum(-1).clamp_min(1e-20)/target_energy)
    quality = window_average(F.softplus(-gain),positions)
    preservation = window_average(distortion,positions)
    return dict(total=quality+preservation,quality=quality,target_distortion=preservation,
        mean_SI_SDR_gain_db=window_average(gain,positions),mean_target_power_transfer_db=window_average(transfer,positions))


def detection_terms(model,batch):
    logits = model.detector(batch['detection_features'],batch['directions'],batch['candidate_valid'])
    original = selection_objective_terms(logits,batch['labels'],batch['candidate_valid'],batch['group_masks'],batch['ranking_pairs'])
    all_terms = F.binary_cross_entropy_with_logits(logits,batch['labels'][...,1].clamp_min(0).to(logits.dtype),reduction='none')
    windows = []
    for i in range(len(logits)):
        numerator = logits.sum()*0; denominator = 0.
        for g,weight in enumerate(GROUP_WEIGHTS):
            group = batch['group_masks'][i,g]
            if group.any():
                numerator = numerator+weight*all_terms[i,group].mean(); denominator += weight
        if denominator:
            windows.append(numerator/denominator)
    bce = torch.stack(windows).mean() if windows else logits.sum()*0
    return dict(total=bce+.25*original['source_ranking'],weighted_group_bce=bce,source_ranking=original['source_ranking'])


def apply_mask(spectrum,mask,samples=SAMPLES):
    from research.directional_tf_mask import istft
    m = np.asarray(mask,float)
    assert m.shape == (67,64) and np.isfinite(m).all()
    assert np.all((m >= .8-1e-6) & (m <= 1.2+1e-6))
    return istft(np.asarray(spectrum)*np.c_[m[:,:1],np.repeat(m,4,axis=1)],samples)
