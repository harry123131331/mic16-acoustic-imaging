"""PC research input, shared causal TF masker and component supervision.

Feature extraction accepts observed PCM, physical candidate directions and an
independent quiet reference. Source components are confined to a separate
training-label function. This is not a deployed model or streaming front end.
"""
from __future__ import annotations
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from scipy.signal import resample_poly
from acoustic_camera.geometry_2d import expected_pair_lags_azel
from mic16_config import Mic16Config, compact32_pairs
from research.candidate_joint_selection_objective_v2 import selection_objective_terms, GROUP_WEIGHTS

VERSION = "mic16_direction_conditioned_tf_mask_v1_20261008"
RATE, FFT, HOP, BANDS, INPUTS, HIDDEN, CAP = 16000, 512, 256, 64, 10, 12, 16
FEATURE_NAMES = ("short_pair_compensated_cos", "short_pair_compensated_sin",
    "long_pair_compensated_cos", "long_pair_compensated_sin", "compensated_plane_coherence",
    "beam_log_power_div40", "beam_to_channel_power_db_div20",
    "beam_to_other_candidate_max_db_div20", "beam_to_quiet_channel_db_div20", "frequency_div8000")


def resample(values):
    return resample_poly(np.asarray(values, float), 256, 625, axis=0)


def stft(values):
    """512 periodic Hann, 50% overlap, known zero padding; [T,257,...]."""
    x = np.asarray(values, np.float64)
    if x.ndim not in (1, 2) or len(x) < FFT or not np.isfinite(x).all():
        raise ValueError("finite [N] or [N,M] signal required")
    right = HOP+(-len(x)) % HOP
    padding = [(HOP, right)]+[(0, 0)]*(x.ndim-1)
    padded = np.pad(x, padding)
    frames = np.stack([padded[start:start+FFT] for start in range(0, len(padded)-FFT+1, HOP)])
    window = np.hanning(FFT+1)[:-1]
    shape = (1, FFT)+((1,) if x.ndim == 2 else ())
    return np.fft.rfft(frames*window.reshape(shape), axis=1)


def istft(spectrum, samples):
    y = np.asarray(spectrum)
    if y.ndim != 2 or y.shape[1] != FFT//2+1 or not np.isfinite(y).all():
        raise ValueError("finite mono STFT[T,257] required")
    window = np.hanning(FFT+1)[:-1]
    blocks = np.fft.irfft(y, n=FFT, axis=1)
    length = (len(y)-1)*HOP+FFT
    signal, norm = np.zeros(length), np.zeros(length)
    for i, block in enumerate(blocks):
        start = i*HOP
        signal[start:start+FFT] += block*window
        norm[start:start+FFT] += window*window
    selection = slice(HOP, HOP+samples)
    if len(signal[selection]) != samples or np.any(norm[selection] < 1e-8):
        raise ValueError("STFT frame count does not cover the requested signal")
    return signal[selection]/norm[selection]


def band_mean(values):
    x = np.asarray(values)
    if x.shape[1] != 257:
        raise ValueError("frequency axis must contain 257 bins")
    return x[:, 1:].reshape(len(x), BANDS, 4, *x.shape[2:]).mean(axis=2)


def apply_mask(spectrum, mask, samples):
    """Constant 4-bin band masks; DC shares band0. No output normalization."""
    m = np.asarray(mask, float)
    if m.shape != (len(spectrum), BANDS) or not np.isfinite(m).all() or np.any((m < 0) | (m > 1)):
        raise ValueError("mask[T,64] must lie in [0,1]")
    full = np.concatenate((m[:, :1], np.repeat(m, 4, axis=1)), axis=1)
    return istft(np.asarray(spectrum)*full, samples)


def observed_features(frames, directions, reference, beam_waveforms):
    """No components, activity, labels, source count or identity arguments."""
    cfg = Mic16Config()
    d = np.asarray(directions, float)
    pcm = np.asarray(frames, float).reshape(-1, 16)
    quiet = np.asarray(reference, float).reshape(-1, 16)
    beams = np.asarray(beam_waveforms, float)
    if d.shape != (len(beams), 2) or not 1 <= len(d) <= CAP or beams.shape[1:] != (len(pcm),):
        raise ValueError("matching directions[C,2], beams[C,N], 1..16 candidates required")
    if not np.isfinite(d).all() or np.any(np.abs(d) >= 90):
        raise ValueError("finite front-hemisphere estimated directions required")
    observed = stft(resample(pcm))
    quiet_spectrum = stft(resample(quiet))
    beam_spectra = np.stack([stft(resample(b)) for b in beams])
    power = np.stack([band_mean(np.abs(b)**2) for b in beam_spectra])
    channel_power = band_mean((np.abs(observed)**2).mean(axis=2))
    quiet_power = np.median(band_mean((np.abs(quiet_spectrum)**2).mean(axis=2)), axis=0)
    pairs = compact32_pairs(cfg)
    cross = observed[:, :, pairs[:, 0]]*observed[:, :, pairs[:, 1]].conj()
    magnitude = np.abs(cross)
    phat = np.divide(cross, magnitude, out=np.zeros_like(cross), where=magnitude > cfg.phat_epsilon)
    frequencies = np.fft.rfftfreq(FFT, 1/RATE)
    features = np.zeros((len(d), len(observed), BANDS, INPUTS), np.float32)
    for c, direction in enumerate(d):
        lags = expected_pair_lags_azel(*direction, cfg, clip=False).astype(float)/cfg.sample_rate
        signature = np.exp(1j*2*np.pi*frequencies[:, None]*lags[None, :])
        compensated = phat*signature[None, :, :].conj()
        short = band_mean(compensated[:, :, :16].mean(axis=2))
        long = band_mean(compensated[:, :, 16:].mean(axis=2))
        plane = band_mean(np.abs(compensated.mean(axis=2)))
        features[c, :, :, :5] = np.stack((short.real, short.imag, long.real, long.imag, plane), axis=-1)
        other = np.max(np.delete(power, c, axis=0), axis=0) if len(d) > 1 else power[c]
        features[c, :, :, 5] = np.clip(10*np.log10(np.maximum(power[c], 1e-20)), -100, 80)/40
        for k, denominator, low, high in ((6, channel_power, -40, 20), (7, other, -40, 20), (8, quiet_power, -20, 40)):
            ratio = 10*np.log10(np.maximum(power[c], 1e-20)/np.maximum(denominator, 1e-20))
            features[c, :, :, k] = np.clip(ratio, low, high)/20
        features[c, :, :, 9] = frequencies[1:].reshape(BANDS, 4).mean(1)[None, :]/8000
    weight = power/np.maximum(power.mean(axis=(1, 2), keepdims=True), 1e-20)
    assert np.isfinite(features).all() and np.isfinite(weight).all()
    return features, weight.astype(np.float32), beam_spectra


def component_mask_targets(component_spectra, background_spectrum, anchors, candidates):
    """Supervision only: component-wise amplitude IRM at the SAME candidate beam.

    spectra[C,S,T,257], background[C,T,257], anchors map candidate->source.
    Unassigned negatives have a zero target; gray/padding is excluded by loss.
    Coherent source cross terms are not part of the independent power ratio.
    """
    bg = np.asarray(background_spectrum)
    components = np.asarray(component_spectra)
    if bg.ndim != 3 or bg.shape[0] != candidates or bg.shape[-1] != 257:
        raise ValueError("background spectra[C,T,257] required")
    if components.shape[:1] != (candidates,) or components.shape[2:] != bg.shape[1:]:
        raise ValueError("component spectra[C,S,T,257] required")
    target = np.zeros((candidates, bg.shape[1], BANDS), np.float32)
    for c, source in anchors.items():
        powers = np.stack([band_mean(np.abs(s)**2) for s in components[c]])
        denominator = powers.sum(0)+band_mean(np.abs(bg[c])**2)
        target[c] = np.sqrt(powers[source]/np.maximum(denominator, 1e-20))
    assert np.isfinite(target).all() and np.all((target >= 0) & (target <= 1))
    return target


class CausalSeparableBlock(nn.Module):
    def __init__(self, dilation):
        super().__init__()
        self.dilation = dilation
        self.depthwise = nn.Conv2d(HIDDEN, HIDDEN, (3, 3), groups=HIDDEN, dilation=(dilation, 1))
        self.pointwise = nn.Conv2d(HIDDEN, HIDDEN, 1)

    def forward(self, x):
        padded = F.pad(x, (1, 1, 2*self.dilation, 0))
        return F.relu(x+self.pointwise(F.relu(self.depthwise(padded))))


class DirectionalTFMask(nn.Module):
    """Shared candidate network; causal along time, joint mask and validity."""
    def __init__(self):
        super().__init__()
        self.project = nn.Conv2d(INPUTS, HIDDEN, 1)
        self.blocks = nn.Sequential(CausalSeparableBlock(1), CausalSeparableBlock(2))
        self.mask_head = nn.Conv2d(HIDDEN, 1, 1)
        self.validity_hidden = nn.Linear(2*HIDDEN+2, HIDDEN)
        self.validity_output = nn.Linear(HIDDEN, 1)

    def forward(self, features, candidate_valid):
        if features.ndim != 5 or features.shape[-2:] != (BANDS, INPUTS):
            raise ValueError("features[B,C,T,64,10] required")
        b, c, t = features.shape[:3]
        if candidate_valid.shape != (b, c) or not 1 <= c <= CAP:
            raise ValueError("candidate_valid[B,C], C=1..16 required")
        x = features.reshape(b*c, t, BANDS, INPUTS).permute(0, 3, 1, 2)
        h = self.blocks(F.relu(self.project(x)))
        mask_logits = self.mask_head(h).squeeze(1)
        mask = torch.sigmoid(mask_logits)
        coherence = x[:, 4]
        pooled = torch.cat((h.mean((2, 3)), h.amax((2, 3)), mask.mean((1, 2)).unsqueeze(1),
                            (mask*coherence).mean((1, 2)).unsqueeze(1)), dim=1)
        validity = self.validity_output(F.relu(self.validity_hidden(pooled))).reshape(b, c)
        mask_logits = mask_logits.reshape(b, c, t, BANDS)
        valid = candidate_valid.bool()
        return dict(validity_logits=validity.masked_fill(~valid, -20.),
                    mask_logits=mask_logits.masked_fill(~valid[:, :, None, None], -20.))


def objective(output, labels, valid, groups, ranking_pairs, targets, spectral_weight):
    selection = selection_objective_terms(output["validity_logits"], labels, valid, groups, ranking_pairs)
    logits = output["mask_logits"]
    if targets.shape != logits.shape or spectral_weight.shape != logits.shape:
        raise ValueError("mask target and spectral weight must match mask logits")
    errors = ((torch.sigmoid(logits)-targets)**2*spectral_weight).mean(dim=(-2, -1))
    zero = logits.sum()*0
    windows = []
    for i in range(len(logits)):
        numerator, denominator = zero, 0.
        for group, weight in enumerate(GROUP_WEIGHTS):
            selected = groups[i, group]
            if selected.any():
                numerator = numerator+weight*errors[i, selected].mean()
                denominator += weight
        if denominator:
            windows.append(numerator/denominator)
    mask_loss = torch.stack(windows).mean() if windows else zero
    return dict(total=selection["total"]+mask_loss, selection_total=selection["total"],
                weighted_group_bce=selection["weighted_group_bce"], source_ranking=selection["source_ranking"],
                spectral_mask_mse=mask_loss)


def model_budget(model, frames=67):
    params = sum(p.numel() for p in model.parameters())
    mac = CAP*frames*BANDS*(INPUTS*HIDDEN+2*(9*HIDDEN+HIDDEN*HIDDEN)+HIDDEN)+CAP*((2*HIDDEN+2)*HIDDEN+HIDDEN)
    return dict(parameters=params, fp32_weight_bytes=params*4, dense_mac_per_window=mac,
        candidates=CAP, frames=frames, frequency_bands=BANDS, feature_channels=INPUTS,
        excludes="resampling/STFT/phase compensation/physical candidates/WDAS/reductions/activations/IO; no FPGA resource claim")
