"""Metadata-only supervision proposal for a neural candidate selection chain.

Preserve existing assigned speech positives and true short-emission ambiguity.
Ordinary ON-speech neighbors are selection negatives, rather than untrained
directions that remain eligible for output. Nothing here enters model input.
No acoustic generation, model, optimizer or output-decoder mutation.
"""
from __future__ import annotations
import numpy as np
from scipy.optimize import linear_sum_assignment
from acoustic_camera.valid_source_detection import angular_distance_deg

VERSION = "mic16_joint_selection_supervision_consistency_v2_20261008"
GROUP_NAMES = ("assigned_speech_positive", "localized_nuisance_negative", "near_ON_speech_selection_negative", "other_negative")
VALID_STATES = {"ON_WINDOW_OCCUPANCY", "GRAY_SHORT_EMISSION", "OFF"}


def revised_selection_supervision(directions, original_labels, candidate_valid, source_states):
    d = np.asarray(directions, float)
    original = np.asarray(original_labels)
    valid = np.asarray(candidate_valid, bool)
    if d.ndim != 2 or d.shape[1] != 2 or not 1 <= len(d) <= 16 or original.shape != d.shape or valid.shape != (len(d),):
        raise ValueError("directions[C,2], labels[C,2], valid[C] with C=1..16 required")
    if not np.isfinite(d).all() or not np.isin(original, [-1, 0, 1]).all():
        raise ValueError("finite directions and labels in {-1,0,1} required")
    on, short = [], []
    for index, state in enumerate(source_states):
        point = np.asarray(state["direction"], float)
        if state["role"] not in ("speech", "nuisance") or state["state"] not in VALID_STATES or point.shape != (2,) or not np.isfinite(point).all():
            raise ValueError("invalid supervision-only source state")
        if state["role"] == "speech":
            if state["state"] == "ON_WINDOW_OCCUPANCY":
                on.append((index, point))
            elif state["state"] == "GRAY_SHORT_EMISSION":
                short.append(point)
    on_distance = np.array([[angular_distance_deg(*p, *point) for _, point in on] for p in d]).reshape(len(d), len(on))
    short_distance = np.array([[angular_distance_deg(*p, *point) for point in short] for p in d]).reshape(len(d), len(short))
    near_on = np.any(on_distance <= 15, axis=1)
    near_short = np.any(short_distance <= 15, axis=1)
    revised = original.astype(np.int8).copy()
    convert = valid & (original[:, 1] == -1) & near_on & ~near_short
    revised[convert, 1] = 0
    # 即使与活动源重叠，真实短发声的不确定性仍保留。
    assert np.array_equal(revised[:, 0], original[:, 0])
    assert np.array_equal(revised[:, 1] == 1, original[:, 1] == 1)
    assert np.all(revised[valid & near_short & (original[:, 1] == -1), 1] == -1)
    positive = valid & (revised[:, 1] == 1)
    mechanical = valid & (revised[:, 0] == 1) & (revised[:, 1] == 0)
    near_negative = valid & (revised[:, 1] == 0) & near_on & ~mechanical
    other = valid & (revised[:, 1] == 0) & ~mechanical & ~near_negative
    groups = np.stack((positive, mechanical, near_negative, other))
    assert np.array_equal(groups.sum(axis=0), (valid & (revised[:, 1] >= 0)).astype(int))

    # 目标排序分组由标签构造，不能用于推理输入。
    # 其他源已分配的正例不作为本组竞争负例。
    anchors = np.flatnonzero(positive)
    ranking = []
    n, k = len(anchors), len(on)
    if n:
        cost = np.full((n+k, n+k), 10000.)
        distance = on_distance[anchors]
        cost[:n, :k] = np.where(distance <= 10, distance, 10000.)
        cost[:n, k:] = 1000.; cost[n:, :k] = 1000.; cost[n:, k:] = 0.
        rows, cols = linear_sum_assignment(cost)
        for row, col in zip(rows, cols):
            if row < n and col < k and distance[row, col] <= 10:
                anchor = int(anchors[row])
                competitors = np.flatnonzero(valid & (revised[:, 1] == 0) & (on_distance[:, col] <= 15)).tolist()
                ranking.append(dict(source_state_index=on[col][0], anchor_candidate=anchor,
                    competing_negative_candidates=competitors, anchor_error_deg=float(distance[row, col])))
        assert len(ranking) == len(anchors), "Every frozen speech-positive anchor must match an ON source within10deg"
    return dict(version=VERSION, labels=revised, group_masks=groups,
                converted_candidates=np.flatnonzero(convert).tolist(),
                preserved_short_gray_candidates=np.flatnonzero(valid & near_short & (original[:, 1] == -1)).tolist(),
                ranking_targets=ranking, inference_input=False,
                purpose="Joint source selection, not binary speech presence at every direction")
