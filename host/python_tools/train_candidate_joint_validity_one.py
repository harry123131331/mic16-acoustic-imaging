"""Prepare a proposal, then explicitly execute one newly authorized run.

Defaults are read-only. Existing synthesis/features/baseline are reused.
No imports of a synthesis entry, no old weights, no threshold search.
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
import sys
import time
import numpy as np
import torch

from research.candidate_joint_validity_network import CandidateJointValidityTCN, joint_validity_loss, decode_joint_validity, model_budget, VERSION
from research.candidate_rescue_network import score_window, aggregate

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "results/candidate_window_preparation_20261008"
OLD = ROOT / "results/candidate_rescue_one_run_20261008"
PREP = ROOT / "results/candidate_joint_validity_preparation_20261008"
OUT = ROOT / "results/candidate_joint_validity_one_run_20261008"
DOC = ROOT / "docs/03_algorithm/MIC16_风扇误判诊断与神经判别主链训练方案_20261008.md"
SEED, EPOCHS, BATCH = 20261008, 24, 16
THRESHOLDS = (.50, .65, .80)


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(directory, name, value):
    with (directory/name).open("x", encoding="utf-8") as f:
        json.dump(value, f, ensure_ascii=False, indent=2, allow_nan=False)


def verify_sealed():
    old = read(OLD / "protocol_lock.json")
    for section in ("protected_sha256", "source_sha256", "inputs_sha256"):
        for relative, expected in old[section].items():
            assert sha(ROOT/relative) == expected, relative
    current = read(OLD/"current_result.json")
    for name, expected in current["authoritative_sha256"].items():
        assert sha(OLD/name) == expected, name
    assert sha(ROOT/current["report_path"]) == current["report_sha256"]
    assert read(ROOT/"results/candidate_semantic_failure_diagnosis_20261008/independent_verification.json")["failed"] == 0
    return old


def proposed_protocol():
    old = verify_sealed()
    sources = [Path(__file__), ROOT/"python_tools/research/candidate_joint_validity_network.py",
               ROOT/"python_tools/tests/test_candidate_joint_validity_network.py", DOC]
    inputs = [DATA/"feature_cache.npz", DATA/"window_records.json", OLD/"baseline_predictions.json",
              OLD/"summary.json", OLD/"current_result.json", OLD/"protocol_lock.json"]
    return dict(status="PROPOSED_ADDITIONAL_SINGLE_RUN_NOT_STARTED", date="2026-10-08",
        model_version=VERSION, architecture="88->16 projection; causal k3 d1/d2 TCN; mean16 + observed7 statistics ->16->1 validity logit",
        model_budget=model_budget(CandidateJointValidityTCN()), seed=SEED, training_runs_cap=1,
        max_epochs=EPOCHS, batch=BATCH, train_windows=256, development_CAL_windows=64,
        optimizer=dict(name="AdamW", learning_rate=.001, weight_decay=.0001, gradient_norm_clip=5, schedule=None),
        objective="Per-window equal mean of available speech-positive/direct-localized-nuisance-negative/other-negative BCE groups; speech-gray/invalid ignored",
        checkpoint="first strictly smallest CAL three-group loss, one final checkpoint",
        thresholds=list(THRESHOLDS), selection="maximum development F1, then fewer FP, then larger registered threshold",
        product_acceptance="Practical functional metrics to be assessed; beating pure physics/zero incremental FP not automatic product gates",
        inference="All candidate acceptance/rejection controlled by neural validity. Physical reference retained only as explicit separate fallback/control",
        new_data_generation=False, new_TEST=False, old_weights_loaded=False, formal_builds=False,
        run_output_limit_bytes=4*1024**2, source_sha256={str(p.relative_to(ROOT)): sha(p) for p in sources},
        inputs_sha256={str(p.relative_to(ROOT)): sha(p) for p in inputs}, protected_sha256=old["protected_sha256"])


def prepare():
    assert not PREP.exists(), "Preserve an existing proposal"
    proposal = proposed_protocol()
    PREP.mkdir()
    save(PREP, "protocol_proposal.json", proposal)
    print(json.dumps(dict(status=proposal["status"], model_budget=proposal["model_budget"], new_training_runs=0), ensure_ascii=False))


def verify_proposal():
    proposal = read(PREP/"protocol_proposal.json")
    for section in ("source_sha256", "inputs_sha256", "protected_sha256"):
        for relative, expected in proposal[section].items():
            assert sha(ROOT/relative) == expected, relative
    verify_sealed()
    return proposal


def context(record):
    gray = [s["direction"] for s in record["supervision"]["source_states"]
            if s["role"] == "speech" and s["state"] == "GRAY_SHORT_EMISSION"]
    return record["ON_speech_directions"], gray, record["scenario"] in ("noise", "fan", "motor", "pure_tail")


def evaluate(rows):
    return aggregate(rows)


def run_once(authorization):
    # 此文本须描述对话中实际收到的人类授权；
    # 命令行参数只是执行保护，不构成授权证据。
    assert authorization.strip(), "Record the actual human authorization"
    proposal = verify_proposal()
    assert not OUT.exists(), "One-run output already exists: no retry/overwrite"
    with np.load(DATA/"feature_cache.npz") as z:
        features, labels, valid, directions = z["features"], z["labels"], z["candidate_valid"], z["directions"]
    records = read(DATA/"window_records.json")
    train = np.array([i for i, r in enumerate(records) if r["role"] == "TRAIN"])
    cal = np.array([i for i, r in enumerate(records) if r["role"] == "CAL"])
    assert len(train) == 256 and len(cal) == 64
    torch.set_num_threads(2); torch.manual_seed(SEED); torch.use_deterministic_algorithms(True)
    model = CandidateJointValidityTCN().cpu()
    x = torch.from_numpy(features).float(); y = torch.from_numpy(labels); mask = torch.from_numpy(valid)
    OUT.mkdir()
    frozen = dict(proposal, status="FROZEN_BEFORE_ADDITIONAL_SINGLE_TRAINING", authorization=authorization,
                  protocol_proposal_sha256=sha(PREP/"protocol_proposal.json"))
    save(OUT, "protocol_lock.json", frozen)
    initial_sha = hashlib.sha256(b"".join(p.detach().numpy().tobytes() for p in model.state_dict().values())).hexdigest()
    save(OUT, "training_started.json", dict(authorization=authorization, seed=SEED, initial_state_sha256=initial_sha,
                                           max_epochs=EPOCHS, training_runs=1, no_automatic_retry=True))
    optimizer = torch.optim.AdamW(model.parameters(), lr=.001, weight_decay=.0001)
    rng = np.random.default_rng(SEED)
    history, best, best_epoch, best_state, steps = [], float("inf"), 0, None, 0

    def losses():
        model.eval()
        with torch.no_grad():
            outputs = model(x)
            return float(joint_validity_loss(outputs[train], y[train], mask[train])), float(joint_validity_loss(outputs[cal], y[cal], mask[cal]))

    initial_losses = losses()
    start = time.perf_counter()
    for epoch in range(1, EPOCHS+1):
        model.train(); order = rng.permutation(train); max_gradient = 0.
        for offset in range(0, len(order), BATCH):
            ids = order[offset:offset+BATCH]
            optimizer.zero_grad(set_to_none=True)
            loss = joint_validity_loss(model(x[ids]), y[ids], mask[ids])
            assert torch.isfinite(loss)
            loss.backward()
            gradient = float(torch.nn.utils.clip_grad_norm_(model.parameters(), 5))
            assert np.isfinite(gradient)
            max_gradient = max(max_gradient, gradient)
            optimizer.step(); steps += 1
        train_loss, cal_loss = losses()
        assert np.isfinite([train_loss, cal_loss]).all()
        if cal_loss < best:
            best, best_epoch = cal_loss, epoch
            best_state = {name: value.detach().clone() for name, value in model.state_dict().items()}
        row = dict(epoch=epoch, train_loss=train_loss, cal_loss=cal_loss, best_epoch_so_far=best_epoch,
                   maximum_preclip_gradient_norm=max_gradient, optimizer_steps=steps)
        history.append(row); print(json.dumps(row), flush=True)
    assert steps == 384 and best_state is not None
    model.load_state_dict(best_state); model.eval()
    with torch.no_grad():
        logits = model(x).numpy(); probabilities = 1/(1+np.exp(-logits.astype(float)))
    torch.save(dict(state_dict=best_state, model_version=VERSION, epoch=best_epoch,
                    cal_loss=best, protocol_sha256=sha(OUT/"protocol_lock.json")), OUT/"best_checkpoint.pt")
    np.savez_compressed(OUT/"candidate_predictions.npz", logits=logits, probabilities=probabilities)
    save(OUT, "training_history.json", dict(initial_losses=initial_losses, epochs=history, selected_epoch=best_epoch,
                                           selected_cal_loss=best, optimizer_steps=steps, elapsed_seconds=time.perf_counter()-start))
    predictions, table = {}, []
    for threshold in THRESHOLDS:
        entries = []
        for i in cal:
            points, chosen = decode_joint_validity(directions[i], probabilities[i], valid[i], threshold)
            truth, gray, clear = context(records[i])
            metrics = dict(score_window(points, truth, gray), truth_count=len(truth), clear_zero=clear)
            entries.append(dict(index=int(i), scene_id=records[i]["scene_id"], outputs=points, selected_candidates=chosen, metrics=metrics))
        predictions[str(threshold)] = entries
        table.append(dict(threshold=threshold, metrics=evaluate([r["metrics"] for r in entries])))
    selected = max(table, key=lambda r: (r["metrics"]["f1"], -r["metrics"]["fp"], r["threshold"]))
    selected_rows = predictions[str(selected["threshold"])]
    baseline = read(OLD/"baseline_predictions.json")
    assert [r["index"] for r in baseline] == cal.tolist()
    strata = {}
    for field in ("room_id", "scenario"):
        strata[field] = {name: evaluate([r["metrics"] for r in selected_rows if records[r["index"]][field] == name])
                        for name in sorted({records[i][field] for i in cal})}
    strict = []
    for row in selected_rows:
        truth, gray, clear = context(records[row["index"]])
        strict.append(dict(score_window(row["outputs"], truth), truth_count=len(truth), clear_zero=clear))
    save(OUT, "CAL_all_threshold_predictions.json", predictions)
    save(OUT, "summary.json", dict(status="SINGLE_RUN_COMPLETED_PENDING_INDEPENDENT_VERIFICATION", training_runs=1,
        selected_epoch=best_epoch, selected_cal_loss=best, selected_threshold=selected["threshold"],
        threshold_table=table, main_chain=selected["metrics"], strata=strata, strict_gray_as_fp=evaluate(strict),
        physical_reference=read(OLD/"summary.json")["baseline_v1"], model_budget=proposal["model_budget"],
        physical_comparison_is_product_gate=False, independent_TEST_runs=0, formal_builds=0))
    verify_proposal()
    assert sum(p.stat().st_size for p in OUT.iterdir()) <= proposal["run_output_limit_bytes"]


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare-proposal", action="store_true")
    parser.add_argument("--run-once", action="store_true")
    parser.add_argument("--authorization", default="")
    args = parser.parse_args()
    assert not (args.prepare_proposal and args.run_once)
    if args.prepare_proposal:
        prepare()
    elif args.run_once:
        run_once(args.authorization)
    else:
        print(json.dumps(dict(status="NO_TRAINING_REQUESTED", proposal_exists=PREP.exists(), run_directory_exists=OUT.exists())))
