"""Read-only proposal coverage on sealed corrected VAL; never trains or deploys.

All proposal rules are fixed before scoring. Truth enters only the diagnostic
matching step. Proposal coverage is an oracle ceiling, not detection recall.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python_tools"))
from acoustic_camera.tf_zone_histogram_2d import (
    TfZoneThresholds, TfZoneVote, select_tf_zone_candidates,
)
from acoustic_camera.valid_source_detection import angular_distance_deg
from acoustic_camera.geometry_2d import direction_unit_vector

OUT = ROOT / "results/pre_gate_candidate_coverage_v2_20261007"
BASE = ROOT / "results/tf_zone_pilot_difference_audit_20261006"
NEURAL = ROOT / "results/tf_zone_cctcn_repaired_one_run_20261007"
THRESHOLD = ROOT / "results/tf_zone_histogram_dev_v1/selected_thresholds.json"
MATCH_DEG, PROPOSAL_CAP, PROPOSAL_NMS_DEG = 10., 16, 10.


def read(p):
    return json.loads(Path(p).read_text(encoding="utf-8"))


def sha(p):
    h = hashlib.sha256()
    with Path(p).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def save(name, obj):
    with (OUT / name).open("x", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2, allow_nan=False)
        f.write("\n")


def vectors(directions):
    a = np.asarray(directions, dtype=float).reshape(-1, 2)
    az, el = np.deg2rad(a).T
    return np.stack((np.cos(el)*np.sin(az), np.sin(el), np.cos(el)*np.cos(az)), 1)


def distances(a, b):
    a = np.asarray(a, dtype=float).reshape(-1, 2)
    b = np.asarray(b, dtype=float).reshape(-1, 2)
    az, el = np.deg2rad(a).T
    bz, bl = np.deg2rad(b).T
    cosine = np.sin(el[:, None])*np.sin(bl[None, :]) + np.cos(el[:, None])*np.cos(bl[None, :])*np.cos(az[:, None]-bz[None, :])
    result = np.rad2deg(np.arccos(np.clip(cosine, -1, 1)))
    # 规范标量函数决定精确10度边界归属。
    for i, j in np.argwhere(np.abs(result - 10.) < 1e-8):
        result[i, j] = angular_distance_deg(*a[i], *b[j])
    return result


def maximum_matching(directions, truths):
    """DP over truth subsets; one proposal can match at most one truth."""
    errors = distances(directions, truths)
    states = {0: (0., [])}
    for p in range(len(directions)):
        previous = list(states.items())
        for mask, (cost, pairs) in previous:
            for k in range(len(truths)):
                if mask & (1 << k) or errors[p, k] > MATCH_DEG:
                    continue
                new = mask | (1 << k)
                next_cost = cost + float(errors[p, k])
                if new not in states or next_cost < states[new][0]:
                    states[new] = (next_cost, pairs + [[p, k, float(errors[p, k])]])
    mask = max(states, key=lambda m: (m.bit_count(), -states[m][0]))
    return states[mask][1]


def clusters(votes, eligible, radius):
    # 末次出现及插入顺序与正式种子字典保持一致。
    seeds = {(float(v[4]), float(v[5])): i for i, v in enumerate(votes) if eligible[i]}
    output = []
    if not seeds:
        return output
    directions = votes[:, 4:6]
    errors = distances(list(seeds), directions)
    unit = np.stack([direction_unit_vector(*d).reshape(3) for d in directions]).astype(np.float64)
    for s, (seed, _) in enumerate(seeds.items()):
        indices = np.flatnonzero((errors[s] <= radius) & eligible)
        local = votes[indices]
        weights = np.prod(np.maximum(local[:, [7, 8, 6]], 0), axis=1)
        weight_sum = float(weights.sum())
        if weight_sum <= 1e-12:
            weights = np.ones(len(indices))
        center = (unit[indices] * weights[:, None]).sum(0)
        center /= max(float(np.linalg.norm(center)), 1e-12)
        direction = [float(np.rad2deg(np.arctan2(center[0], center[2]))),
                     float(np.rad2deg(np.arcsin(np.clip(center[1], -1, 1))))]
        output.append(dict(seed=list(seed), direction=direction, indices=indices.tolist(),
                           votes=len(indices), frames=len(set(local[:, 0].astype(int))),
                           bands=len(set(local[:, 2].astype(int))), weight_sum=weight_sum))
    output.sort(key=lambda c: (c["frames"], c["bands"], c["votes"], c["weight_sum"]), reverse=True)
    return output


def fixed_bounded_pool(raw_clusters):
    selected = []
    for c in raw_clusters:
        if selected and np.any(distances([c["direction"]], [x["direction"] for x in selected]) < PROPOSAL_NMS_DEG):
            continue
        selected.append(c)
        if len(selected) == PROPOSAL_CAP:
            break
    return selected


def main():
    OUT.mkdir(exist_ok=False)
    inputs = [BASE / n for n in ("axis_repair_votes.npz", "axis_repair_predictions.json", "replay.json")]
    inputs += [NEURAL / n for n in ("scene_manifest.json", "v1_val_predictions.json", "feature_cache.npz", "dataset_seal.json")]
    inputs += [THRESHOLD]
    protected = list((ROOT / "python_tools/acoustic_camera").glob("*.py"))
    protected += list((ROOT / "vitis_src").rglob("*.c")) + list((ROOT / "vitis_src").rglob("*.h"))
    protected += list((ROOT / "hls_src").glob("*.cpp")) + list((ROOT / "vivado_rtl").glob("*.v"))
    protected += list((ROOT / "python_tools/mic16_qt").rglob("*.py"))
    for parent in (ROOT / "results/fpga_d4f_rtos_wdas_diagnostics_v7_verified",
                   ROOT / "results/fpga_resource_optimization_wdas_v1/formal_shared_axi_syncreset_route_007"):
        for extension in ("*.elf", "*.BIN", "*.bin", "*.bit", "*.xsa"):
            protected += list(parent.rglob(extension))
    protected += list(NEURAL.glob("*.pt"))
    protected += list((ROOT / "results/tf_zone_cctcn_pilot_v1a_20261006").glob("*.pt"))
    protected += [ROOT / "results/tf_zone_semantic_gate_dev_v1/frozen_gate.json", THRESHOLD]
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(inputs + protected))}
    protocol = dict(status="LOCKED_BEFORE_COVERAGE_SCORING", source_sha256=sha(__file__),
                    inputs_and_protected_sha256=hashes, match_deg=MATCH_DEG,
                    scope="32 already-seen corrected VAL / 36 truths; cache-only diagnostic",
                    unique_pool="Unique raw per-zone argmax directions, before all three vote quality gates",
                    clusters="Unchanged 10-degree fusion and v1 positive weights/ranking; no support/NMS for all-centroid pools",
                    bounded_pool=dict(max_proposals=PROPOSAL_CAP, nms_deg=PROPOSAL_NMS_DEG,
                                      ranking="v1 frame/band/vote/weight lexicographic order on RAW clusters",
                                      role="ONE fixed diagnostic proposal budget; no event decisions, threshold sweep or deployment"),
                    no_training=True, no_new_pcm=True, no_new_test=True, no_runtime_change=True,
                    angular_boundary="production scalar angular_distance_deg at 10 degrees; corrected dot-product draft retained separately",
                    storage_budget_bytes=8*1024*1024)
    save("protocol_lock.json", protocol)
    rows = read(BASE / "axis_repair_predictions.json")
    same = read(NEURAL / "v1_val_predictions.json")
    manifest = [s for s in read(NEURAL / "scene_manifest.json") if s["split"] == "VAL"]
    assert len(rows) == len(same) == len(manifest) == 32
    with np.load(BASE / "axis_repair_votes.npz") as d:
        votes_all = d["votes"].copy()
    assert votes_all.shape == (32, 480, 9) and np.isfinite(votes_all).all()
    t = TfZoneThresholds(**read(THRESHOLD)["thresholds"])
    scene_details, truth_details = [], []
    for si, (row, current, scene) in enumerate(zip(rows, same, manifest)):
        assert row["scene_id"] == current["scene_id"] == scene["scene_id"]
        assert row["truths"] == current["truths"] == scene["truths"]
        assert row["predictions"] == current["predictions"] == scene["v1_outputs"]
        assert all(row[k] == scene[k] for k in ("pcm_sha256", "reference_sha256"))
        a = votes_all[si]
        masks = dict(raw=np.ones(480, bool), energy=a[:, 6] >= t.minimum_energy_excess_db)
        masks["coherence"] = masks["energy"] & (a[:, 7] >= t.minimum_spatial_coherence)
        masks["contrast"] = masks["coherence"] & (a[:, 8] >= t.minimum_peak_contrast)
        grid_pools = {name+"_grid": list(dict.fromkeys(map(tuple, a[mask, 4:6]))) for name, mask in masks.items()}
        raw = clusters(a, masks["raw"], t.fusion_radius_deg)
        quality = clusters(a, masks["contrast"], t.fusion_radius_deg)
        supported = [c for c in quality if c["votes"] >= t.minimum_vote_count
                     and c["frames"] >= t.minimum_frame_support and c["bands"] >= t.minimum_coarse_band_support]
        bounded = fixed_bounded_pool(raw)
        production_votes = tuple(TfZoneVote(*[int(x) for x in v[:3]], *map(float, v[3:])) for v in a)
        production = select_tf_zone_candidates(production_votes, t)
        assert np.allclose([[c.azimuth_deg, c.elevation_deg] for c in production], row["spatial_predictions"], atol=1e-9, rtol=0)
        pools = dict(grid_pools, raw_centroids=[c["direction"] for c in raw],
                     raw_bounded16=[c["direction"] for c in bounded],
                     quality_centroids=[c["direction"] for c in quality],
                     supported_before_nms=[c["direction"] for c in supported],
                     v1_spatial=row["spatial_predictions"], v1_final=row["predictions"])
        coverage = {name: dict(proposals=len(p), matched=len(maximum_matching(p, row["truths"])),
                              matched_truth_indices=[x[1] for x in maximum_matching(p, row["truths"])]) for name, p in pools.items()}
        scene_details.append(dict(scene_id=row["scene_id"], scenario=row["scenario"], truths=row["truths"],
                                  pools=pools, coverage=coverage, raw_clusters=raw, quality_clusters=quality,
                                  raw_bounded16_seeds=[c["seed"] for c in bounded]))
        for k, truth in enumerate(row["truths"]):
            near = distances(a[:, 4:6], [truth])[:, 0] <= MATCH_DEG
            counts = {n:dict(votes=int((near & m).sum()), frames=len(set(a[near & m, 0].astype(int))),
                            bands=len(set(a[near & m, 2].astype(int)))) for n, m in masks.items()}
            truth_details.append(dict(scene_id=row["scene_id"], truth_index=k, truth=truth,
                                      local_vote_support=counts,
                                      covered={n:k in c["matched_truth_indices"] for n, c in coverage.items()}))
    summary = {}
    for stage in scene_details[0]["coverage"]:
        parts = [s["coverage"][stage] for s in scene_details]
        counts = [p["proposals"] for p in parts]
        tp = sum(p["matched"] for p in parts)
        summary[stage] = dict(matched=tp, truth_directions=36, coverage=tp/36,
                              total_proposals=sum(counts), min_proposals=min(counts),
                              median_proposals=float(np.median(counts)), max_proposals=max(counts),
                              negative_scenes_nonempty=sum(not s["truths"] and s["coverage"][stage]["proposals"] > 0 for s in scene_details),
                              negative_scene_proposals=sum(s["coverage"][stage]["proposals"] for s in scene_details if not s["truths"]))
    grid = np.array([[az, el] for el in range(-30, 31, 5) for az in range(-60, 61, 5)], float)
    geometry = []
    for s in scene_details:
        fractions = {}
        for stage, pool in s["pools"].items():
            covered_grid = np.any(distances(grid, pool) <= MATCH_DEG, axis=1)
            fractions[stage] = dict(covered_grid_points=int(covered_grid.sum()), fraction=float(covered_grid.mean()))
        geometry.append(dict(scene_id=s["scene_id"], negative=not s["truths"], stages=fractions))
    save("geometry_coverage.json", geometry)
    assert summary["v1_spatial"]["matched"] == 22 and summary["v1_final"]["matched"] == 18
    assert all(sha(ROOT / p) == h for p, h in hashes.items())
    save("scene_coverage.json", scene_details)
    save("truth_coverage.json", truth_details)
    save("summary.json", dict(status="DIAGNOSTIC_ONLY_NO_MODEL_PROMOTION", stages=summary,
                              scenes=32, truths=36, negative_scenes=12, protected_files=len(hashes),
                              v1_unchanged=True, training_runs=0, new_test_runs=0,
                              caution="Inventories/centroids are competing hypotheses, not final outputs; oracle coverage is NOT precision/F1 or achievable recall"))
    assert sum(p.stat().st_size for p in OUT.iterdir()) < protocol["storage_budget_bytes"]
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
