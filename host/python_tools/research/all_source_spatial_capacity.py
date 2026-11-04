"""Observed-only capacity control; no learned scores, labels or deployment."""
import numpy as np
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION = 'mic16_spatial_nms10_cap32_control_20261011'
CAP, NMS_DEG = 32, 10.


def spatial_candidates(raw_clusters, cap=CAP):
    """Continue the original support-ranked greedy NMS sequence to cap32.

    Input order owns ranking. Centroids are never moved, merged or reranked.
    The original cap16 result must be an exact prefix; checked by the probe.
    """
    if cap not in (16, 32):
        raise ValueError('only preregistered cap16 baseline and cap32 control allowed')
    selected = []
    for candidate in raw_clusters:
        direction = np.asarray(candidate['direction'], float)
        if direction.shape != (2,) or not np.isfinite(direction).all():
            raise ValueError('finite observed azimuth/elevation required')
        if selected:
            previous = np.asarray([c['direction'] for c in selected], float)
            az, el = np.radians(direction)
            paz, pel = np.radians(previous).T
            errors = np.degrees(np.arccos(np.clip(
                np.sin(el)*np.sin(pel) + np.cos(el)*np.cos(pel)*np.cos(az-paz), -1, 1)))
            for j in np.flatnonzero(np.abs(errors-NMS_DEG) < 1e-8):
                errors[j] = angular_distance_deg(*direction, *previous[j])
            if np.any(errors < NMS_DEG):
                continue
        selected.append(candidate)
        if len(selected) == cap:
            break
    return selected
