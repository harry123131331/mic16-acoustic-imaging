"""Read-only default; prepare a concrete trial; run only on NEW human approval.

Only architecture changes relative to selection-v2. One seed, <=24 epochs,
same frozen data/objective/decoder. Does not publish an unverified model.
"""
from __future__ import annotations
import os
for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
    os.environ[name] = "2"
import argparse
import hashlib
import json
from pathlib import Path
import time
import numpy as np
import torch
import train_candidate_joint_selection_v2_one as base
from research.candidate_neighborhood_tcn import CandidateNeighborhoodTCN, model_budget, VERSION
from research.candidate_joint_selection_objective_v2 import selection_objective_terms
from research.candidate_joint_validity_network import decode_joint_validity

ROOT = base.ROOT
PREP = ROOT/"results/candidate_neighborhood_preparation_20261008"
OUT = ROOT/"results/candidate_neighborhood_one_run_20261008"
DIAG = ROOT/"results/candidate_rejection_diagnosis_20261008"
read, sha, save = base.read, base.sha, base.save
SEED, EPOCHS, BATCH, THRESHOLDS = 20261008, 24, 16, (.5, .65, .8)


def prepare():
    assert not PREP.exists() and not OUT.exists(), "Preserve proposal/run"
    protected = base.verify_proposal()["protected_sha256"]
    verification = read(DIAG/"verification.json")
    assert verification["failed"] == 0 and verification["tests_run"] == 7
    for name, expected in verification["output_sha256"].items():
        assert sha(DIAG/name) == expected
    torch.manual_seed(SEED)
    model = CandidateNeighborhoodTCN()
    arrays = base.load_training_arrays()
    assert len(arrays["train_indices"]) == 256 and len(arrays["cal_indices"]) == 64
    sources = [Path(__file__), ROOT/"python_tools/research/candidate_neighborhood_tcn.py",
        ROOT/"python_tools/research/candidate_joint_validity_network.py",
        ROOT/"python_tools/research/candidate_joint_selection_objective_v2.py",
        ROOT/"python_tools/tests/test_candidate_neighborhood_tcn.py",
        ROOT/"python_tools/train_candidate_joint_selection_v2_one.py"]
    inputs = [base.DATA/"feature_cache.npz", base.DATA/"window_records.json",
        base.SUP/"selection_supervision_v2.npz", base.SUP/"supervision_metadata.json",
        base.OUT/"best_checkpoint.pt", base.OUT/"candidate_predictions.npz",
        base.OUT/"training_history.json", DIAG/"summary.json", DIAG/"verification.json"]
    protocol = dict(status="PROPOSED_NOT_AUTHORIZED_NOT_STARTED", date="2026-10-08", model_version=VERSION,
        architecture="unchanged causal TCN ->23 pooled; own23 + own-minus-neighbor-mean23 + own-minus-neighbor-max23 + count/15 + exists ->71->16->1",
        hypothesis="explicit local comparison may improve speech retention versus spatial duplicate/offset rejection; gain is not established",
        observed_inputs="original 20x88 features + physical candidate azimuth/elevation + padding validity only",
        neighborhood_radius_deg=15, radius_choice="fixed to existing selection-neighbor radius, not swept",
        model_budget=model_budget(model), training_runs_cap=1, max_epochs=EPOCHS,
        optimizer_steps_cap=384, seed=SEED, batch=BATCH, from_scratch=True, old_weights_loaded=False,
        optimizer=dict(name="AdamW", learning_rate=.001, weight_decay=.0001, gradient_norm_clip=5),
        objective="unchanged selection-v2: per-window four-group BCE weights 2/1/1/1 +0.25 softplus ranking, margin1; gray/invalid ignored",
        checkpoint="first strictly minimum development CAL total loss; no extra checkpoint performance scan",
        thresholds=list(THRESHOLDS), threshold_selection="highest development F1, then fewer FP, then larger threshold",
        decoder="unchanged score descending, ties original index; 10-degree NMS; max8 sources",
        TRAIN_windows=256, CAL_windows=64, independent_TEST=False, new_acoustic_windows=0,
        intended_follow_through="verify saved logits independently; same64-window detection/localization -> all estimated-direction WDAS outputs -> fixed6 heatmaps/audio/timing -> same central report",
        evaluation="seven system items; retain FN/no-output examples; no conditional-error-only success claim",
        acceptance="joint speech detection/localization/enhancement usability; pure-physics win is not the acceptance gate; exploratory CAL gains cannot certify generalization",
        constraints="no radius/width/loss sweep, automatic retrain, hidden physical accept branch, formal build or deployment",
        execution=dict(device="CPU", fp32=True, torch_threads=2, deterministic=True, output_limit_bytes=4*1024**2),
        authorization="new actual human approval required because prior single-run budget is consumed",
        source_sha256={str(p.relative_to(ROOT)): sha(p) for p in sources},
        inputs_sha256={str(p.relative_to(ROOT)): sha(p) for p in inputs}, protected_sha256=protected)
    PREP.mkdir(); save(PREP, "protocol_proposal.json", protocol)
    print(json.dumps(dict(status=protocol["status"], model_budget=protocol["model_budget"], new_training_runs=0), ensure_ascii=False))


def verify_proposal():
    protocol = read(PREP/"protocol_proposal.json")
    for section in ("source_sha256", "inputs_sha256", "protected_sha256"):
        for relative, expected in protocol[section].items():
            assert sha(ROOT/relative) == expected, relative
    return protocol


def run_once(authorization):
    assert authorization.strip(), "Record NEW human approval received in the chat; text alone is not approval evidence"
    assert not OUT.exists(), "One-run directory exists: no retry/overwrite"
    protocol = verify_proposal()
    arrays = base.load_training_arrays()
    torch.set_num_threads(2); torch.manual_seed(SEED); torch.use_deterministic_algorithms(True)
    model = CandidateNeighborhoodTCN().cpu()
    x = torch.from_numpy(arrays["features"]).float()
    d = torch.from_numpy(arrays["directions"]).float()
    y, v = torch.from_numpy(arrays["labels"]), torch.from_numpy(arrays["valid"])
    g, p = torch.from_numpy(arrays["groups"]), torch.from_numpy(arrays["ranking_pairs"])
    train, cal, records = arrays["train_indices"], arrays["cal_indices"], arrays["records"]
    OUT.mkdir()
    save(OUT, "protocol_lock.json", dict(protocol, status="FROZEN_BEFORE_AUTHORIZED_RUN", authorization=authorization,
        proposal_sha256=sha(PREP/"protocol_proposal.json")))
    initial_sha = hashlib.sha256(b"".join(t.detach().numpy().tobytes() for t in model.state_dict().values())).hexdigest()
    save(OUT, "training_started.json", dict(authorization=authorization, seed=SEED, training_runs=1,
        max_epochs=EPOCHS, initial_state_sha256=initial_sha, no_automatic_retry=True))
    optimizer = torch.optim.AdamW(model.parameters(), lr=.001, weight_decay=.0001)
    rng = np.random.default_rng(SEED)

    def all_losses():
        model.eval()
        with torch.no_grad():
            logits = model(x, d, v)
            return {role: {key: float(value) for key, value in selection_objective_terms(
                logits[idx], y[idx], v[idx], g[idx], p[idx]).items()} for role, idx in (("TRAIN", train), ("CAL", cal))}

    initial = all_losses()
    history, best, best_state, best_epoch, steps = [], float("inf"), None, 0, 0
    started = time.perf_counter()
    for epoch in range(1, EPOCHS+1):
        model.train(); order = rng.permutation(train); max_gradient = 0.
        for offset in range(0, len(order), BATCH):
            ids = order[offset:offset+BATCH]; optimizer.zero_grad(set_to_none=True)
            loss = selection_objective_terms(model(x[ids], d[ids], v[ids]), y[ids], v[ids], g[ids], p[ids])["total"]
            assert torch.isfinite(loss)
            loss.backward()
            gradient = float(torch.nn.utils.clip_grad_norm_(model.parameters(), 5))
            assert np.isfinite(gradient)
            max_gradient = max(max_gradient, gradient); optimizer.step(); steps += 1
        losses = all_losses()
        assert all(np.isfinite(value) for row in losses.values() for value in row.values())
        if losses["CAL"]["total"] < best:
            best, best_epoch = losses["CAL"]["total"], epoch
            best_state = {name: value.detach().clone() for name, value in model.state_dict().items()}
        row = dict(epoch=epoch, losses=losses, optimizer_steps=steps, best_epoch_so_far=best_epoch,
                   maximum_preclip_gradient_norm=max_gradient)
        history.append(row); print(json.dumps(row), flush=True)
    assert steps == 384 and best_state is not None
    model.load_state_dict(best_state); model.eval()
    with torch.no_grad():
        logits = model(x, d, v).numpy()
    probability = 1/(1+np.exp(-logits.astype(float)))
    torch.save(dict(state_dict=best_state, model_version=VERSION, epoch=best_epoch, cal_loss=best,
                    protocol_sha256=sha(OUT/"protocol_lock.json")), OUT/"best_checkpoint.pt")
    np.savez_compressed(OUT/"candidate_predictions.npz", logits=logits, probabilities=probability)
    save(OUT, "training_history.json", dict(initial_losses=initial, epochs=history, selected_epoch=best_epoch,
        selected_cal_loss=best, optimizer_steps=steps, elapsed_seconds=time.perf_counter()-started))
    predictions, table = {}, []
    for threshold in THRESHOLDS:
        entries = []
        for i in cal:
            points, selected = decode_joint_validity(arrays["directions"][i], probability[i], arrays["valid"][i], threshold)
            truth, gray, clear = base.previous.context(records[i])
            metrics = dict(base.previous.score_window(points, truth, gray), truth_count=len(truth), clear_zero=clear)
            entries.append(dict(index=int(i), scene_id=records[i]["scene_id"], outputs=points,
                                selected_candidates=selected, metrics=metrics))
        predictions[str(threshold)] = entries
        table.append(dict(threshold=threshold, metrics=base.previous.aggregate([r["metrics"] for r in entries])))
    chosen = max(table, key=lambda r: (r["metrics"]["f1"], -r["metrics"]["fp"], r["threshold"]))
    save(OUT, "CAL_all_threshold_predictions.json", predictions)
    save(OUT, "summary.json", dict(status="SINGLE_RUN_COMPLETED_PENDING_INDEPENDENT_VERIFICATION_AND_SYSTEM_SIMULATION",
        training_runs=1, optimizer_steps=steps, selected_epoch=best_epoch, selected_cal_loss=best,
        selected_threshold=chosen["threshold"], threshold_table=table, main_chain=chosen["metrics"],
        model_budget=protocol["model_budget"], functional_acceptance=False, independent_TEST_runs=0,
        formal_builds=0, board_measurements=0))
    verify_proposal()
    assert sum(t.stat().st_size for t in OUT.iterdir()) <= protocol["execution"]["output_limit_bytes"]


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--prepare-proposal", action="store_true")
    action.add_argument("--run-once", action="store_true")
    parser.add_argument("--authorization", default="")
    args = parser.parse_args()
    if args.prepare_proposal:
        prepare()
    elif args.run_once:
        run_once(args.authorization)
    else:
        print(json.dumps(dict(status="READ_ONLY_NO_TRAINING_REQUESTED", proposal_exists=PREP.exists(),
                              run_exists=OUT.exists(), new_training_runs=0)))
