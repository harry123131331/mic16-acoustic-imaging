"""Observed-only reference transfer correction after WDAS, before neural OLA.

This is a bounded PC candidate, not a standard MVDR/MWF implementation.
The neural residual mask is retained. No clean component or label is accepted.
"""
import numpy as np
from research import directional_tf_mask as tf

VERSION = 'mic16_observed_coherence_reference_transfer_v1_20261009'
MAX_GAIN = 2.


def fit(beam_spectrum, reference_spectrum):
    beam, reference = np.asarray(beam_spectrum),np.asarray(reference_spectrum)
    if beam.ndim!=2 or beam.shape[1]!=257 or reference.shape!=beam.shape or len(beam)<2:
        raise ValueError('Matching observed mono spectra[T>=2,257] required')
    if not np.isfinite(beam).all() or not np.isfinite(reference).all():
        raise ValueError('Finite observed spectra required')
    beam_power=np.sum(np.abs(beam)**2,axis=0)
    reference_power=np.sum(np.abs(reference)**2,axis=0)
    cross=np.sum(reference*beam.conj(),axis=0)
    usable=(beam_power>1e-20)&(reference_power>1e-20)
    transfer=np.ones(257,np.complex128)
    transfer[usable]=cross[usable]/beam_power[usable]
    coherence=np.zeros(257)
    coherence[usable]=np.clip(np.abs(cross[usable])**2/(beam_power[usable]*reference_power[usable]),0,1)
    coefficient=1+coherence*(transfer-1)
    clipped=np.abs(coefficient)>MAX_GAIN
    coefficient[clipped]*=MAX_GAIN/np.abs(coefficient[clipped])
    coefficient[[0,-1]]=coefficient[[0,-1]].real
    assert np.isfinite(coefficient).all() and np.max(np.abs(coefficient))<=MAX_GAIN+1e-12
    return coefficient,dict(version=VERSION,observed_frames=len(beam),gain_cap=MAX_GAIN,
        coefficient_magnitude_max=float(np.abs(coefficient).max()),
        coherence_mean=float(coherence.mean()),clipped_bins=int(clipped.sum()),
        clean_reference_used=False,neural_mask_retained=True)


def apply(spectrum, mask, coefficient, samples):
    spectrum,mask,coefficient=np.asarray(spectrum),np.asarray(mask),np.asarray(coefficient)
    if mask.shape!=(len(spectrum),64) or coefficient.shape!=(257,):
        raise ValueError('Mask[T,64] and fixed observed coefficient[257] required')
    if not np.isfinite(mask).all() or np.any((mask<.799999)|(mask>1.200001)):
        raise ValueError('Current neural residual mask must remain in[.8,1.2]')
    full=np.concatenate((mask[:,:1],np.repeat(mask,4,axis=1)),axis=1)
    return tf.istft(spectrum*full*coefficient[None,:],samples)
