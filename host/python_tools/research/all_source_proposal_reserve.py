"""Observed-only bounded proposal reserve; research, not deployed inference.

Keep the complete original pool as an immutable prefix. Reconsider the first
16 distinct discarded centroids in the existing observed support order.
This borrows the preserve-before-decision motivation, not the Soft-NMS formula.
"""
import numpy as np

VERSION='mic16_observed_proposal_reserve_probe_v1_20261011'
RESERVE_CAP=16


def reserve_candidates(raw_clusters, baseline):
    assert 0<len(baseline)<=16
    result=list(baseline)
    selected_seeds={tuple(c['seed']) for c in baseline}
    for candidate in raw_clusters:
        if tuple(candidate['seed']) in selected_seeds:
            continue
        direction=np.asarray(candidate['direction'],float)
        assert direction.shape==(2,) and np.isfinite(direction).all()
        if any(np.max(np.abs(direction-np.asarray(c['direction'])))<=1e-6 for c in result):
            continue
        result.append(candidate)
        if len(result)==len(baseline)+RESERVE_CAP:
            break
    assert result[:len(baseline)]==baseline and len(result)<=32
    return result
