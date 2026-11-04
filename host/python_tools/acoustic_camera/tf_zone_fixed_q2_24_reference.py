"""Bit-oriented Q2.24 reference for the D3c recurrence architecture.

This is the single pre-registered precision escalation after Q2.22 failed the
strict internal vote/contrast equivalence gate.  The algorithm, zones,
thresholds and candidate logic are unchanged; only the fractional precision
increases from 22 to 24 bits.
"""

from __future__ import annotations

import numpy as np

from .tf_zone_streaming_reference import TfZoneStreamingState


TF_ZONE_FIXED_REFERENCE_VERSION = "mic16_tf_zone_recurrence_q2_24_d3c_v1"
TOTAL_BITS = 26
INTEGER_BITS = 2
FRACTION_BITS = TOTAL_BITS - INTEGER_BITS
SCALE = float(1 << FRACTION_BITS)
MINIMUM = -2.0
MAXIMUM = 2.0 - 1.0 / SCALE


def quantize_q2_24(values: np.ndarray) -> np.ndarray:
    source = np.asarray(values)
    if np.iscomplexobj(source):
        real = np.clip(np.rint(np.real(source) * SCALE) / SCALE, MINIMUM, MAXIMUM)
        imag = np.clip(np.rint(np.imag(source) * SCALE) / SCALE, MINIMUM, MAXIMUM)
        return (real + 1j * imag).astype(np.complex64)
    return np.clip(np.rint(source * SCALE) / SCALE, MINIMUM, MAXIMUM).astype(np.float32)


def stream_tf_zone_scores_q2_24(
    observation: np.ndarray,
    zone_slices: tuple[slice, ...],
    state: TfZoneStreamingState,
) -> np.ndarray:
    observed = quantize_q2_24(np.asarray(observation, dtype=np.complex64))
    expected = (int(state.pair_count), int(state.bin_count))
    if observed.shape != expected:
        raise ValueError(f"observation must have shape {expected}, got {observed.shape}")

    phasor = quantize_q2_24(state.initial_phasor)
    rotator = quantize_q2_24(state.bin_rotator)
    scores = np.empty((len(zone_slices), state.grid_point_count), dtype=np.float32)
    for zone_index, zone in enumerate(zone_slices):
        accumulator = np.zeros(state.grid_point_count, dtype=np.float64)
        for bin_index in range(int(zone.start), int(zone.stop)):
            product = np.conj(phasor.astype(np.complex128)) * observed[:, bin_index][None, :]
            accumulator += np.sum(np.real(product), axis=1, dtype=np.float64)
            advanced = phasor.astype(np.complex128) * rotator.astype(np.complex128)
            phasor = quantize_q2_24(advanced)
        scores[zone_index] = (
            accumulator / float(state.pair_count * (int(zone.stop) - int(zone.start)))
        ).astype(np.float32)
    return scores
