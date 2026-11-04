"""Observed-vote coverage ranking, fixed cap16; experimental, not promoted.

Only candidate selection changes. Same raw centroids, 10-degree NMS and
16-candidate interface. No source count, class, truth, scores or training input.
Inspired by statistically dominant-source component discounting; not an
implementation or reproduction of soundfield-microphone SDSCR/PORW/SRP-DNN.
"""
from __future__ import annotations
import numpy as np
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION = 'mic16_residual_vote_candidate_ranking_v1_20261012'
CAP, NMS_DEG = 16, 10.


def residual_bounded_pool(raw_clusters, votes, return_trace=False):
    v = np.asarray(votes, float)
    if v.shape != (480, 9) or not np.isfinite(v).all():
        raise ValueError('480 observed TF-zone votes required')
    weights = np.prod(np.maximum(v[:, [7, 8, 6]], 0), axis=1)
    members = []
    for c in raw_clusters:
        ids = np.asarray(c['indices'], int)
        if ids.ndim != 1 or not len(ids) or not np.all((ids >= 0) & (ids < 480)):
            raise ValueError('observed cluster member indices required')
        members.append(ids)
    credited = np.zeros(480, bool)
    chosen, selected_ids, trace = [], set(), []
    for _ in range(CAP):
        best = None
        for rank, (c, ids) in enumerate(zip(raw_clusters, members)):
            if rank in selected_ids or any(angular_distance_deg(*c['direction'], *s['direction']) < NMS_DEG
                                           for s in chosen):
                continue
            remaining = ids[~credited[ids]]
            if not len(remaining):
                continue
            key = (len(set(v[remaining, 0].astype(int))),
                   len(set(v[remaining, 2].astype(int))), len(remaining),
                   float(weights[remaining].sum()), -rank)
            if best is None or key > best[0]:
                best = (key, rank, remaining)
        if best is None:
            break
        key, rank, remaining = best
        chosen.append(raw_clusters[rank]); selected_ids.add(rank)
        credited[members[rank]] = True
        trace.append(dict(original_cluster_rank=rank + 1, residual_frames=key[0],
                          residual_bands=key[1], residual_votes=key[2], residual_weight=key[3],
                          newly_credited_vote_indices=remaining.tolist(),
                          cumulative_credited_votes=int(credited.sum())))
    assert len(chosen) <= 16
    if chosen:
        assert chosen[0] == raw_clusters[0], 'first observed dominant candidate unchanged'
    return (chosen, trace) if return_trace else chosen
