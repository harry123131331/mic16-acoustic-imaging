"""Prepare or explicitly run ONE new selection-objective trial, from scratch.

Read-only by default. A command-line authorization string is an execution
guard; it must record actual human approval, never manufacture that approval.
Sealed prior models, audio/features, formal products and reports are preserved.
"""
from __future__ import annotations
import os
os.environ["OMP_NUM_THREADS"] = "2"
os.environ["MKL_NUM_THREADS"] = "2"
os.environ["OPENBLAS_NUM_THREADS"] = "2"
import argparse
import hashlib
import json
from pathlib import Path
import time
import numpy as np
import torch

import train_candidate_joint_validity_one as previous
from research.candidate_joint_validity_network import CandidateJointValidityTCN, decode_joint_validity, model_budget, VERSION as BACKBONE_VERSION
from research.candidate_joint_selection_objective_v2 import (
    VERSION, GROUP_WEIGHTS, RANKING_WEIGHT, RANKING_MARGIN, ranking_mask_from_metadata, selection_objective_terms)

ROOT, DATA = previous.ROOT, previous.DATA
SUP = ROOT / "results/candidate_joint_selection_supervision_v2_20261008"
PREP = ROOT / "results/candidate_joint_selection_objective_v2_preparation_20261008"
OUT = ROOT / "results/candidate_joint_selection_objective_v2_one_run_20261008"
DOC = ROOT / "docs/03_algorithm/MIC16_空间选择损失接入与有限训练协议_20261008.md"
SEED, EPOCHS, BATCH = 20261008, 24, 16
THRESHOLDS = (.50, .65, .80)
read, sha, save = previous.read, previous.sha, previous.save


def verify_prior_results():
    previous.verify_proposal()
    protected = previous.verify_sealed()["protected_sha256"]
    for directory in (previous.OUT, SUP):
        current = read(directory / "current_result.json")
        for name, expected in current["authoritative_sha256"].items():
            assert sha(directory / name) == expected, name
        assert sha(ROOT / current["report_path"]) == current["report_sha256"]
    summary = read(SUP / "summary.json")
    for section in ("source_sha256", "inputs_sha256"):
        for relative, expected in summary[section].items():
            assert sha(ROOT / relative) == expected, relative
    assert read(SUP / "independent_verification.json")["failed"] == 0
    return protected


def load_training_arrays():
    """Keep observed features separate from all supervision and source metadata."""
    with np.load(DATA / "feature_cache.npz") as cache:
        features = cache["features"]
        valid = cache["candidate_valid"].astype(bool)
        directions = cache["directions"]
    with np.load(SUP / "selection_supervision_v2.npz") as cache:
        labels, groups = cache["labels"], cache["group_masks"]
    records = read(DATA / "window_records.json")
    metadata = read(SUP / "supervision_metadata.json")
    assert features.shape == (320, 16, 20, 88) and valid.shape == (320, 16)
    assert directions.shape == (320, 16, 2) and labels.shape == (320, 16, 2) and groups.shape == (320, 4, 16)
    assert groups.dtype == bool and np.isfinite(features).all() and np.isfinite(directions).all()
    assert len(records) == len(metadata) == 320
    for i, (record, entry) in enumerate(zip(records, metadata)):
        assert entry["index"] == i and entry["scene_id"] == record["scene_id"] and entry["role"] == record["role"]
    pairs = ranking_mask_from_metadata(metadata, labels, valid)
    train = np.array([i for i, r in enumerate(records) if r["role"] == "TRAIN"])
    cal = np.array([i for i, r in enumerate(records) if r["role"] == "CAL"])
    assert len(train) == 256 and len(cal) == 64 and not np.intersect1d(train, cal).size
    assert np.array_equal(groups.sum(axis=1), (valid & (labels[..., 1] >= 0)).astype(int))
    return dict(features=features, labels=labels, valid=valid, groups=groups, ranking_pairs=pairs,
                directions=directions, records=records, train_indices=train, cal_indices=cal)


def proposed_protocol():
    protected = verify_prior_results()
    assert DOC.exists() and (PREP / "independent_verification.json").exists()
    verification = read(PREP / "independent_verification.json")
    assert verification["failed"] == 0 and verification["new_training_runs"] == 0
    arrays = load_training_arrays()
    sources = [Path(__file__), ROOT / "python_tools/research/candidate_joint_selection_objective_v2.py",
               ROOT / "python_tools/tests/test_candidate_joint_selection_objective_v2.py",
               ROOT / "python_tools/research/candidate_joint_validity_network.py",
               ROOT / "python_tools/research/candidate_joint_selection_supervision_v2.py",
               ROOT / "tools/verification/verify_joint_selection_objective_v2_20261008.py", DOC]
    inputs = [DATA / "feature_cache.npz", DATA / "window_records.json",
              SUP / "selection_supervision_v2.npz", SUP / "supervision_metadata.json", SUP / "current_result.json",
              previous.OUT / "current_result.json", PREP / "independent_verification.json", PREP / "unit_test_results.json"]
    return dict(status="PROPOSED_NEW_SINGLE_RUN_NOT_AUTHORIZED_OR_STARTED", date="2026-10-08", objective_version=VERSION,
        backbone_version=BACKBONE_VERSION, architecture="unchanged 88->16 causal k3 d1/d2 TCN; mean16+observed7 ->16->1",
        model_budget=model_budget(CandidateJointValidityTCN()), seed=SEED, training_runs_cap=1, max_epochs=EPOCHS,
        optimizer_steps_cap=384, batch=BATCH, train_windows=256, development_CAL_windows=64,
        optimizer=dict(name="AdamW", learning_rate=.001, weight_decay=.0001, gradient_norm_clip=5, schedule=None),
        objective=dict(group_names=["assigned_speech_positive", "localized_nuisance_negative", "near_ON_speech_selection_negative", "other_negative"],
            group_weights=list(GROUP_WEIGHTS), available_groups_renormalized_per_window=True,
            ranking_weight=RANKING_WEIGHT, ranking_margin_logits=RANKING_MARGIN,
            ranking_formula="softplus(1 + negative_logit - anchor_logit); mean competitors then nonempty anchors per window; absent ranking=0",
            window_reduction="equal mean across windows with at least one supervised candidate; excludes invalid/short-gray",
            coefficient_choice="fixed engineering hypothesis; no weight/margin sweep or CAL fit"),
        supervision=dict(ON_neighbor_conversions=616, preserved_true_short_gray=14, train_positives=302, CAL_positives=75,
            ranking_pairs_TRAIN=int(arrays["ranking_pairs"][arrays["train_indices"]].sum()),
            ranking_pairs_CAL=int(arrays["ranking_pairs"][arrays["cal_indices"]].sum()),
            inference_input=False),
        checkpoint="first strictly smallest CAL total weighted-group-plus-ranking loss, one final checkpoint",
        thresholds=list(THRESHOLDS), threshold_selection="highest development F1, then fewer FP, then larger registered threshold",
        decoder=dict(duplicate_exclusion_deg=10, maximum_sources=8, tie_break="score descending then original candidate index"),
        evaluation=dict(match_deg=10, report="TP/FP/FN, precision/recall/F1, conditional spherical/azimuth/elevation errors, K accuracy, zero-source/fan/motor/noise/tail and room/scenario strata",
            reused_development_CAL=True, independent_generalization_claim=False,
            beating_pure_physics_is_product_gate=False, acceptance="practical anonymous 2D speech source function; neural network remains required"),
        execution=dict(device="CPU", fp32=True, torch_threads=2, deterministic_algorithms=True, old_weights_loaded=False,
            no_automatic_retry=True, new_acoustic_generation=False, new_TEST=False, formal_builds=False,
            output_limit_bytes=4*1024**2, actual_new_human_authorization_required=True),
        source_sha256={str(p.relative_to(ROOT)): sha(p) for p in sources},
        inputs_sha256={str(p.relative_to(ROOT)): sha(p) for p in inputs}, protected_sha256=protected)


def prepare():
    assert not (PREP / "protocol_proposal.json").exists(), "Preserve an existing proposal"
    proposal = proposed_protocol()
    save(PREP, "protocol_proposal.json", proposal)
    print(json.dumps(dict(status=proposal["status"], model_budget=proposal["model_budget"],
                          supervision=proposal["supervision"], new_training_runs=0), ensure_ascii=False))


def verify_proposal():
    proposal = read(PREP / "protocol_proposal.json")
    for section in ("source_sha256", "inputs_sha256", "protected_sha256"):
        for relative, expected in proposal[section].items():
            assert sha(ROOT / relative) == expected, relative
    verify_prior_results()
    return proposal


def run_once(authorization):
    if not authorization.strip():
        raise ValueError("Actual new human approval must be recorded; previous run's approval is already consumed")
    assert not OUT.exists(), "One-run output exists: no retry or overwrite"
    proposal = verify_proposal()
    arrays = load_training_arrays()
    torch.set_num_threads(2); torch.manual_seed(SEED); torch.use_deterministic_algorithms(True)
    model = CandidateJointValidityTCN().cpu()
    x = torch.from_numpy(arrays["features"]).float()
    y, valid = torch.from_numpy(arrays["labels"]), torch.from_numpy(arrays["valid"])
    groups, pairs = torch.from_numpy(arrays["groups"]), torch.from_numpy(arrays["ranking_pairs"])
    train, cal, records = arrays["train_indices"], arrays["cal_indices"], arrays["records"]
    OUT.mkdir()
    save(OUT, "protocol_lock.json", dict(proposal, status="FROZEN_BEFORE_NEW_SINGLE_RUN", authorization=authorization,
                                         protocol_proposal_sha256=sha(PREP / "protocol_proposal.json")))
    initial_sha = hashlib.sha256(b"".join(p.detach().numpy().tobytes() for p in model.state_dict().values())).hexdigest()
    save(OUT, "training_started.json", dict(authorization=authorization, seed=SEED, initial_state_sha256=initial_sha,
                                           max_epochs=EPOCHS, training_runs=1, no_automatic_retry=True))
    optimizer = torch.optim.AdamW(model.parameters(), lr=.001, weight_decay=.0001)
    rng = np.random.default_rng(SEED)
    history, best, best_epoch, best_state, steps = [], float("inf"), 0, None, 0

    def all_losses():
        model.eval()
        with torch.no_grad():
            logits = model(x)  # 神经推理只接收观测特征。
            return {role: {key: float(value) for key, value in selection_objective_terms(
                logits[ids], y[ids], valid[ids], groups[ids], pairs[ids]).items()} for role, ids in (("TRAIN", train), ("CAL", cal))}

    initial_losses = all_losses()
    start = time.perf_counter()
    for epoch in range(1, EPOCHS + 1):
        model.train(); order = rng.permutation(train); max_gradient = 0.
        for offset in range(0, len(order), BATCH):
            ids = order[offset:offset+BATCH]
            optimizer.zero_grad(set_to_none=True)
            loss = selection_objective_terms(model(x[ids]), y[ids], valid[ids], groups[ids], pairs[ids])["total"]
            assert torch.isfinite(loss)
            loss.backward()
            gradient = float(torch.nn.utils.clip_grad_norm_(model.parameters(), 5))
            assert np.isfinite(gradient)
            max_gradient = max(max_gradient, gradient)
            optimizer.step(); steps += 1
        losses = all_losses()
        assert all(np.isfinite(v) for row in losses.values() for v in row.values())
        cal_loss = losses["CAL"]["total"]
        if cal_loss < best:
            best, best_epoch = cal_loss, epoch
            best_state = {name: value.detach().clone() for name, value in model.state_dict().items()}
        row = dict(epoch=epoch, losses=losses, best_epoch_so_far=best_epoch, optimizer_steps=steps,
                   maximum_preclip_gradient_norm=max_gradient)
        history.append(row); print(json.dumps(row), flush=True)
    assert steps == 384 and best_state is not None
    model.load_state_dict(best_state); model.eval()
    with torch.no_grad():
        logits = model(x).numpy(); probabilities = 1 / (1 + np.exp(-logits.astype(float)))
    torch.save(dict(state_dict=best_state, backbone_version=BACKBONE_VERSION, objective_version=VERSION,
                    epoch=best_epoch, cal_loss=best, protocol_sha256=sha(OUT / "protocol_lock.json")), OUT / "best_checkpoint.pt")
    np.savez_compressed(OUT / "candidate_predictions.npz", logits=logits, probabilities=probabilities)
    save(OUT, "training_history.json", dict(initial_losses=initial_losses, epochs=history, selected_epoch=best_epoch,
        selected_cal_loss=best, optimizer_steps=steps, elapsed_seconds=time.perf_counter()-start))
    predictions, table = {}, []
    for threshold in THRESHOLDS:
        entries = []
        for i in cal:
            points, chosen = decode_joint_validity(arrays["directions"][i], probabilities[i], arrays["valid"][i], threshold)
            truth, gray, clear = previous.context(records[i])
            metrics = dict(previous.score_window(points, truth, gray), truth_count=len(truth), clear_zero=clear)
            entries.append(dict(index=int(i), scene_id=records[i]["scene_id"], outputs=points, selected_candidates=chosen, metrics=metrics))
        predictions[str(threshold)] = entries
        table.append(dict(threshold=threshold, metrics=previous.aggregate([row["metrics"] for row in entries])))
    selected = max(table, key=lambda r: (r["metrics"]["f1"], -r["metrics"]["fp"], r["threshold"]))
    selected_rows = predictions[str(selected["threshold"])]
    strata = {field: {name: previous.aggregate([r["metrics"] for r in selected_rows if records[r["index"]][field] == name])
                     for name in sorted({records[i][field] for i in cal})} for field in ("room_id", "scenario")}
    strict = []
    for row in selected_rows:
        truth, _, clear = previous.context(records[row["index"]])
        strict.append(dict(previous.score_window(row["outputs"], truth), truth_count=len(truth), clear_zero=clear))
    save(OUT, "CAL_all_threshold_predictions.json", predictions)
    save(OUT, "summary.json", dict(status="SINGLE_RUN_COMPLETED_PENDING_INDEPENDENT_VERIFICATION", training_runs=1,
        objective_version=VERSION, selected_epoch=best_epoch, selected_cal_loss=best, selected_threshold=selected["threshold"],
        threshold_table=table, main_chain=selected["metrics"], strata=strata, strict_gray_as_fp=previous.aggregate(strict),
        previous_neural_main_chain=read(previous.OUT / "summary.json")["main_chain"], model_budget=proposal["model_budget"],
        independent_TEST_runs=0, formal_builds=0, physical_comparison_is_product_gate=False))
    verify_proposal()
    assert sum(p.stat().st_size for p in OUT.iterdir()) <= proposal["execution"]["output_limit_bytes"]


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--prepare-proposal", action="store_true")
    actions.add_argument("--run-once", action="store_true")
    parser.add_argument("--authorization", default="")
    args = parser.parse_args()
    if args.prepare_proposal:
        prepare()
    elif args.run_once:
        run_once(args.authorization)
    else:
        print(json.dumps(dict(status="NO_TRAINING_REQUESTED", proposal_exists=(PREP / "protocol_proposal.json").exists(),
                              run_directory_exists=OUT.exists(), new_training_runs=0)))
