"""Class-agnostic source supervision and inference; no sound-type input.

Uses the existing 28,673-parameter detector architecture. Source truth enters
supervision/evaluation only, never the detector or its output decoder.
"""
from __future__ import annotations
import math
import numpy as np
import torch
from torch.nn import functional as F
from scipy.optimize import linear_sum_assignment
from acoustic_camera.valid_source_detection import angular_distance_deg
from research.multisource_split_network import DetectionNetwork
from research.candidate_joint_validity_network import decode_joint_validity

VERSION = 'mic16_all_source_detection_20ms_v1_20261011'
SAMPLE_RATE = 39062.5
WINDOW_SAMPLES = 40960
MIN_SUPPORT_SAMPLES = math.ceil(.020 * SAMPLE_RATE)
MATCH_DEG, GRAY_DEG, THRESHOLD = 10., 15., .65


def source_context(record):
    """Existing dry activity counts, irrespective of speech/nuisance role.

    At least 20 ms cumulative direct-arrival-aligned dry emission is ON.
    This is a window occupancy rule for existing simulation masks, not a
    framewise detector, SPL gate, or general event annotation standard.
    """
    active, gray, source_indices = [], [], []
    for index, source in enumerate(record['source_details']):
        count = source['active_samples']
        if isinstance(count, bool) or not isinstance(count, (int, np.integer)):
            raise ValueError('integer dry activity count required')
        if not 0 <= count <= WINDOW_SAMPLES:
            raise ValueError('activity count outside window')
        point = np.asarray(source['direction'], float)
        if point.shape != (2,) or not np.isfinite(point).all():
            raise ValueError('finite source azimuth/elevation required')
        if count >= MIN_SUPPORT_SAMPLES:
            active.append(point.tolist()); source_indices.append(index)
        elif count > 0:
            gray.append(point.tolist())
    return active, gray, source_indices


def candidate_targets(directions, valid, record):
    """One anchor per active source via maximum cardinality/minimum angle.

    Unassigned candidates within 15 degrees of any active/short source are
    ignored; candidates outside are negative. No semantic negative group.
    Duplicate outputs remain FP at evaluation, even if ignored in this loss.
    """
    d, v = np.asarray(directions, float), np.asarray(valid, bool)
    if d.ndim != 2 or d.shape[1] != 2 or v.shape != (len(d),):
        raise ValueError('directions[C,2], valid[C] required')
    if not np.isfinite(d).all():
        raise ValueError('finite observed candidate directions required')
    truth, gray, source_ids = source_context(record)
    ids = np.flatnonzero(v)
    y = np.full(len(d), -1, np.int8)
    y[ids] = 0
    for c in ids:
        if any(angular_distance_deg(*d[c], *p) <= GRAY_DEG for p in truth + gray):
            y[c] = -1
    pairs = []
    if truth and len(ids):
        error = np.array([[angular_distance_deg(*p, *d[c]) for c in ids] for p in truth])
        # 未匹配源代价1000，禁止候选边代价10000。
        # 已有源至多4个，因此先匹配数量，再考虑角度代价。
        cost = np.full((len(truth), len(ids) + len(truth)), 1000.)
        cost[:, :len(ids)] = np.where(error <= MATCH_DEG, error, 10000.)
        ti, ci = linear_sum_assignment(cost)
        for t, c in zip(ti, ci):
            if c < len(ids) and error[t, c] <= MATCH_DEG:
                candidate = int(ids[c]); y[candidate] = 1
                pairs.append(dict(candidate=candidate, truth=int(t),
                                  source_index=source_ids[t], error_deg=float(error[t, c])))
    return dict(labels=y, truth=truth, gray=gray, matching=pairs,
                source_indices=source_ids, uncovered=len(truth)-len(pairs))


def all_source_loss(logits, targets, valid):
    """Equal positive/negative BCE groups per window; -1/padding ignored.

    A located active fan or motor has exactly the same positive objective as
    speech. Label class, identity, source count and truth directions are not
    model inputs. No legacy speech-only ranking or nuisance-negative loss.
    """
    if logits.shape != targets.shape or valid.shape != targets.shape:
        raise ValueError('matching [B,C] logits/targets/valid required')
    if not torch.isfinite(logits).all() or not torch.all((targets >= -1) & (targets <= 1)):
        raise ValueError('finite logits and targets in {-1,0,1} required')
    if not torch.all((targets == -1) | (targets == 0) | (targets == 1)):
        raise ValueError('discrete targets required')
    terms = F.binary_cross_entropy_with_logits(logits, targets.clamp_min(0).to(logits.dtype), reduction='none')
    windows = []
    for i in range(len(logits)):
        groups = [terms[i][valid[i].bool() & (targets[i] == value)].mean()
                  for value in (1, 0) if (valid[i].bool() & (targets[i] == value)).any()]
        if groups:
            windows.append(torch.stack(groups).mean())
    return torch.stack(windows).mean() if windows else logits.sum()*0


def decode_sources(directions, scores, valid):
    """Anonymous per-window IDs; scores uncalibrated, no class or true K."""
    points, selected = decode_joint_validity(directions, scores, valid, THRESHOLD)
    return [dict(source_id=i+1, azimuth_deg=p[0], elevation_deg=p[1],
                 detection_score=float(scores[c]), candidate_index=c)
            for i, (p, c) in enumerate(zip(points, selected))]


def detector_budget():
    model = DetectionNetwork()
    count = sum(p.numel() for p in model.parameters())
    if count != 28673:
        raise RuntimeError('detector architecture changed')
    return dict(parameters=count, fp32_weight_bytes=count*4,
                dense_MAC_per_16_candidate_window=16*20*(106*32+3*3*32*32)+16*(215*64+64*32+32),
                observation_seconds=WINDOW_SAMPLES/SAMPLE_RATE,
                excludes='FFT/PHAT/spatial features/reductions/neighborhood/IO; not measured FPGA cost')
