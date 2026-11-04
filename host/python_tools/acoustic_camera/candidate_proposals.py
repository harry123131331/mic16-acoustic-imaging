"""Candidate-proposal response fusion for the isolated PC research branch.

The helpers in this module only improve the *proposal table*.  They do not
accept a source, classify speech, or alter the formal FPGA/PS localization
contract.  A full-band SRP-PHAT map is fused with normalized speech-subband
maps so that a weak but frequency-consistent speech peak is less likely to be
discarded before B6C/B6S can evaluate it.
"""

from __future__ import annotations

import numpy as np


CANDIDATE_PROPOSAL_VERSION = "mic16_multiband_candidate_proposal_v1"


def normalize_spatial_response(
    response: np.ndarray,
    *,
    floor_percentile: float = 5.0,
) -> np.ndarray:
    """Map a finite spatial response to [0, 1] using a robust low floor."""

    values = np.asarray(response, dtype=np.float64)
    if values.ndim not in (1, 2) or values.size == 0:
        raise ValueError("response must be a non-empty 1-D or 2-D map")
    if not np.all(np.isfinite(values)):
        raise ValueError("response must be finite")
    percentile = float(floor_percentile)
    if not 0.0 <= percentile < 100.0:
        raise ValueError("floor_percentile must lie in [0, 100)")
    floor = float(np.percentile(values, percentile))
    maximum = float(np.max(values))
    dynamic = maximum - floor
    if dynamic <= 1e-12:
        return np.zeros(values.shape, dtype=np.float32)
    return np.clip((values - floor) / dynamic, 0.0, 1.0).astype(np.float32)


def fuse_speech_band_responses(
    wideband_response: np.ndarray,
    subband_responses: np.ndarray,
    relative_band_energy: np.ndarray,
    *,
    wideband_weight: float = 0.50,
    minimum_relative_band_energy: float = 0.08,
    floor_percentile: float = 5.0,
) -> np.ndarray:
    """Fuse normalized wideband and active speech-subband SRP responses.

    Subband maps are independently normalized before an energy-weighted mean.
    If no band is active, the normalized wideband response is returned.
    """

    wide = np.asarray(wideband_response, dtype=np.float64)
    bands = np.asarray(subband_responses, dtype=np.float64)
    energy = np.asarray(relative_band_energy, dtype=np.float64).reshape(-1)
    if bands.ndim != wide.ndim + 1 or bands.shape[1:] != wide.shape:
        raise ValueError("subband_responses must have shape [band,*wideband_shape]")
    if energy.size != bands.shape[0]:
        raise ValueError("relative_band_energy length must equal subband count")
    if not np.all(np.isfinite(bands)) or not np.all(np.isfinite(energy)):
        raise ValueError("subband responses and relative energies must be finite")
    weight = float(wideband_weight)
    if not 0.0 <= weight <= 1.0:
        raise ValueError("wideband_weight must lie in [0,1]")
    minimum = float(minimum_relative_band_energy)
    if not 0.0 <= minimum <= 1.0:
        raise ValueError("minimum_relative_band_energy must lie in [0,1]")

    normalized_wide = normalize_spatial_response(
        wide, floor_percentile=floor_percentile
    )
    active = (energy >= minimum) & (energy > 0.0)
    if not np.any(active):
        return normalized_wide
    normalized_bands = np.stack(
        [
            normalize_spatial_response(item, floor_percentile=floor_percentile)
            for item in bands[active]
        ],
        axis=0,
    ).astype(np.float64)
    active_weights = energy[active]
    active_weights /= float(np.sum(active_weights))
    band_mean = np.tensordot(active_weights, normalized_bands, axes=(0, 0))
    fused = weight * normalized_wide.astype(np.float64) + (1.0 - weight) * band_mean
    return np.clip(fused, 0.0, 1.0).astype(np.float32)
