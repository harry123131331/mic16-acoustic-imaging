"""Read-only PC full TF-zone maps before frozen vote/candidate rejection.

Shares the original float PHAT projector (hash-locked by the experiment).
No neural output is mixed into formal v1. Quality requires a separate quiet
reference, not oracle source-separated audio. FFT-domain dB is NOT SPL.
"""
from __future__ import annotations

import numpy as np

from .tf_zone_histogram_2d import TfZoneProjector


def extract_neural_features(projector: TfZoneProjector, frames, noise_frames):
    values, noise = np.asarray(frames), np.asarray(noise_frames)
    expected = (int(projector.cfg.frame_samples), int(projector.cfg.channels))
    for array in (values, noise):
        if array.ndim != 3 or array.shape[0] < 1 or array.shape[1:] != expected:
            raise ValueError("frames/reference must be [T,frame_samples,channels]")
        if not np.isfinite(array).all():
            raise ValueError("frames/reference must be finite")
    if np.shares_memory(values, noise):
        raise ValueError("noise reference must be independent of the input window")

    spectra = np.stack([projector._frame_spectra(frame) for frame in values])
    observations = np.stack([projector._observation(frame) for frame in spectra])
    power = np.mean(np.abs(spectra) ** 2, axis=-1)
    noise_power = np.stack([
        np.mean(np.abs(projector._frame_spectra(frame)) ** 2, axis=-1) for frame in noise
    ])
    maps, levels, floors = [], [], []
    for zone in projector.zone_slices:
        signature = projector.signatures[:, :, zone]
        observed = observations[:, :, zone]
        score = np.real(np.einsum("gpf,tpf->tg", np.conj(signature), observed, optimize=True))
        score /= float(signature.shape[1] * signature.shape[2])
        maps.append(score.reshape(values.shape[0], *projector.grid.shape))
        levels.append(10 * np.log10(np.maximum(np.mean(power[:, zone], axis=1), 1e-20)))
        floors.append(np.median(10 * np.log10(np.maximum(np.mean(noise_power[:, zone], axis=1), 1e-20))))
    raw_level = np.stack(levels, axis=1)
    excess = raw_level - np.asarray(floors)[None, :]
    quality = np.concatenate((np.clip(excess, -20, 40) / 20,
                              np.clip(raw_level, -100, 80) / 40), axis=1)
    return np.stack(maps, axis=1).astype(np.float32), quality.astype(np.float32)
