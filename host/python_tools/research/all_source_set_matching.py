"""Proposed training-only one-to-one supervision for anonymous sound sources.

This is a constrained candidate rescoring adaptation, not a DETR/GossipNet
reproduction. The inference architecture, threshold and NMS remain unchanged.
Truth is used only here to construct the loss, never in detector.forward.
"""
from __future__ import annotations

import numpy as np
import torch
from scipy.optimize import linear_sum_assignment

from acoustic_camera.valid_source_detection import angular_distance_deg
from research.all_source_detection import (
    MATCH_DEG, GRAY_DEG, source_context, all_source_loss,
)

VERSION = 'mic16_all_source_score_matched_supervision_v2_20261011'


def score_matched_targets(directions, valid, logits, record):
    """Maximize coverage, then total detached logit inside the 10-degree gate.

    An active source has at most one positive candidate, a candidate at most
    one source. Every unmatched valid candidate is negative, including nearby
    duplicates. Only <20ms short-emission regions remain ignored (15 degrees).
    No confidence threshold or true-K cap is used during assignment. An absent
    physical candidate remains an uncovered source; labels cannot create it.

    Normalization and dummy costs make cardinality dominate ALL score choices.
    Ties use SciPy's deterministic assignment for a fixed input order. Exact
    tied duplicates have no intrinsic identity; their chosen index can change
    when permuted. The loss/matched count, not that identity, must be invariant.
    """
    d = np.asarray(directions, float)
    v = np.asarray(valid, bool)
    s = np.asarray(logits, float)
    if (d.ndim != 2 or d.shape[1] != 2 or not 1 <= len(d) <= 16
            or v.shape != (len(d),) or s.shape != v.shape):
        raise ValueError('directions[1..16,2], valid[C], logits[C] required')
    if not np.isfinite(d).all() or not np.isfinite(s).all():
        raise ValueError('finite observed directions and detached logits required')
    truth, gray, source_ids = source_context(record)
    ids = np.flatnonzero(v)
    labels = np.full(len(d), -1, np.int8)
    labels[ids] = 0
    for c in ids:
        if any(angular_distance_deg(*d[c], *g) <= GRAY_DEG for g in gray):
            labels[c] = -1
    pairs = []
    if truth and len(ids):
        error = np.array([[angular_distance_deg(*p, *d[c]) for c in ids] for p in truth])
        scale = max(1., float(np.abs(s[ids]).max()))
        k = len(truth)
        dummy = float(2*k + 1)
        forbidden = (k + 1)*dummy + 2*k + 1
        cost = np.full((k, len(ids)+k), dummy)
        cost[:, :len(ids)] = np.where(error <= MATCH_DEG, -s[ids]/scale, forbidden)
        ti, ci = linear_sum_assignment(cost)
        for t, c in zip(ti, ci):
            if c < len(ids) and error[t, c] <= MATCH_DEG:
                candidate = int(ids[c])
                labels[candidate] = 1
                pairs.append(dict(candidate=candidate, truth=int(t),
                                  source_index=source_ids[t], error_deg=float(error[t, c])))
    return dict(labels=labels, truth=truth, gray=gray, matching=pairs,
                source_indices=source_ids, uncovered=len(truth)-len(pairs))


def set_matching_loss(logits, directions, valid, records):
    """Detached discrete assignment + the baseline's balanced BCE reduction.

    Positive and negative group means remain equally weighted per window.
    Assignment is recomputed for each TRAIN batch; it is not differentiated.
    Extra ranking/count/semantic objectives and new loss coefficients are absent.
    """
    if (logits.ndim != 2 or directions.shape != (*logits.shape, 2)
            or valid.shape != logits.shape or len(records) != len(logits)):
        raise ValueError('matching batched logits/directions/valid/records required')
    detached = logits.detach().cpu().numpy()
    observed = directions.detach().cpu().numpy()
    mask = valid.detach().cpu().numpy()
    targets = [score_matched_targets(observed[i], mask[i], detached[i], r)
               for i, r in enumerate(records)]
    y = torch.as_tensor(np.stack([t['labels'] for t in targets]), device=logits.device)
    return all_source_loss(logits, y, valid)
