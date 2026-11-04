"""Training-only target reconstruction and component fidelity supervision.

The frozen1034-parameter inference is reused without alteration. Components
are never arguments of its forward or observed feature extraction.
"""
from __future__ import annotations
import numpy as np
import torch
from research.directional_tf_mask import DirectionalTFMask, model_budget, BANDS
from research.candidate_joint_selection_objective_v2 import selection_objective_terms

VERSION = 'mic16_directional_target_fidelity_objective_v2_20261009'
PRESERVATION_WEIGHT = .25
ENHANCEMENT_WEIGHT = 1.


def grouped_sum(x):
    """The actual mask applies to bins1..256 in groups4, with DC in group0."""
    values = np.asarray(x, float)
    if values.ndim != 2 or values.shape[1] != 257 or not np.isfinite(values).all():
        raise ValueError('finite [T,257] coefficients required')
    result = values[:, 1:].reshape(len(values), BANDS, 4).sum(axis=2)
    result[:, 0] += values[:, 0]
    return result


def reconstruction_coefficients(mixture_spectrum, target_spectrum):
    """Positive-anchor label only, same estimated beam and observed weights.

    q=sum|Y|²/E_S, r=sumRe(Y*conj(S))/E_S, s=sum|S|²/E_S.
    Sum(q*m²-2*r*m+s) equals the normalized complex spectral error;
    sum(s*(1-m)²) is target-component spectral energy removed by that mask.
    Coherent source interference remains in Y. DC is represented exactly.
    """
    y, s = np.asarray(mixture_spectrum), np.asarray(target_spectrum)
    if y.shape != s.shape or y.ndim != 2 or y.shape[1] != 257:
        raise ValueError('matching complex STFT[T,257] required')
    if not np.isfinite(y).all() or not np.isfinite(s).all():
        raise ValueError('finite spectra required')
    target_energy = float(np.sum(np.abs(s)**2))
    if not target_energy > 1e-20:
        raise ValueError('nonzero target component energy required')
    result = np.stack((grouped_sum(np.abs(y)**2), grouped_sum(np.real(y*s.conj())),
                       grouped_sum(np.abs(s)**2)), axis=-1)/target_energy
    assert np.isfinite(result).all()
    return result, target_energy


def positive_enhancement_terms(mask_logits, positive, coefficients):
    if mask_logits.ndim != 4 or mask_logits.shape[-1] != BANDS:
        raise ValueError('mask_logits[B,C,T,64] required')
    if positive.shape != mask_logits.shape[:2] or positive.dtype != torch.bool:
        raise ValueError('positive[B,C] boolean required')
    if coefficients.shape != (*mask_logits.shape, 3):
        raise ValueError('coefficients[B,C,T,64,3] required')
    zero = mask_logits.sum()*0
    reconstruction, preservation = [], []
    for i in range(len(mask_logits)):
        selected = positive[i]
        if not selected.any():
            continue
        c = coefficients[i, selected]
        if not torch.isfinite(c).all() or torch.any(c[..., 0] < 0) or torch.any(c[..., 2] < 0):
            raise ValueError('finite positive-anchor q/s powers required')
        m = torch.sigmoid(mask_logits[i, selected])
        q, r, s = c.unbind(-1)
        reconstruction.append((q*m*m-2*r*m+s).sum((-2, -1)).mean())
        preservation.append(((1-m)**2*s).sum((-2, -1)).mean())
    sa = torch.stack(reconstruction).mean() if reconstruction else zero
    distortion = torch.stack(preservation).mean() if preservation else zero
    return dict(spectral_signal_approximation=sa, target_component_distortion=distortion,
                enhancement_total=sa+PRESERVATION_WEIGHT*distortion)


def objective(output, labels, valid, groups, ranking_pairs, coefficients):
    selection = selection_objective_terms(output['validity_logits'], labels, valid, groups, ranking_pairs)
    enhancement = positive_enhancement_terms(output['mask_logits'], groups[:, 0], coefficients)
    return dict(total=selection['total']+ENHANCEMENT_WEIGHT*enhancement['enhancement_total'],
        selection_total=selection['total'], weighted_group_bce=selection['weighted_group_bce'],
        source_ranking=selection['source_ranking'], **enhancement)
