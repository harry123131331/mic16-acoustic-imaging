"""Prepare or execute one finite joint detection/enhancement trial.

Read-only by default. --run-once requires actual new human approval in chat;
an authorization string alone is not evidence that approval was received.
"""
import os
for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
    os.environ[name] = "2"
os.environ["CUBLAS_WORKSPACE_CONFIG"] = ":4096:8"
from pathlib import Path
import argparse
import hashlib
import json
import time
import numpy as np
import torch
import prepare_directional_tf_mask as preparation
from research.directional_tf_mask import DirectionalTFMask, objective, model_budget, VERSION
from research.candidate_joint_validity_network import decode_joint_validity

ROOT, PREP = preparation.ROOT, preparation.OUT
OUT = ROOT / "results/directional_tf_mask_one_run_20261008"
read, sha, save = preparation.read, preparation.sha, preparation.save
SEED, EPOCHS, BATCH, THRESHOLDS = 20261008, 24, 16, (.5, .65, .8)


def setup(device="cuda"):
    torch.set_num_threads(2); torch.manual_seed(SEED)
    torch.use_deterministic_algorithms(True)
    torch.backends.cudnn.benchmark = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cuda.matmul.allow_tf32 = False
    if device == "cuda":
        assert torch.cuda.is_available(), "Frozen CUDA trial requires available GPU; do not silently change device"
        torch.cuda.manual_seed_all(SEED)
    return torch.device(device)


def state_sha(model):
    return hashlib.sha256(b"".join(t.detach().cpu().numpy().tobytes() for t in model.state_dict().values())).hexdigest()


def load_arrays():
    arrays = {name: np.load(PREP / file, mmap_mode="r") for name, file in
              (("features", "observed_features.npy"), ("targets", "mask_targets.npy"), ("weights", "spectral_weights.npy"))}
    with np.load(PREP / "selection_arrays.npz") as data:
        arrays.update({k: data[k].copy() for k in data.files})
    assert arrays["features"].shape == (320, 16, 67, 64, 10)
    assert len(arrays["train_indices"]) == 256 and len(arrays["cal_indices"]) == 64
    return arrays


def batch(arrays, ids, device):
    ids = np.asarray(ids)
    result = {}
    for key in ("features", "targets", "weights"):
        # 将磁盘FP16观测/标签提升为FP32，不使用混合精度优化器。
        result[key] = torch.from_numpy(np.array(arrays[key][ids], dtype=np.float32, copy=True)).to(device)
    for key in ("labels", "candidate_valid", "group_masks", "ranking_pairs"):
        result[key] = torch.from_numpy(arrays[key][ids].copy()).to(device)
    return result


def terms(model, b):
    output = model(b["features"], b["candidate_valid"])
    return objective(output, b["labels"], b["candidate_valid"], b["group_masks"], b["ranking_pairs"], b["targets"], b["weights"])


def verify_proposal():
    p = read(PREP / "protocol_proposal.json")
    for section in ("source_sha256", "input_sha256", "protected_sha256"):
        for path, expected in p[section].items():
            assert sha(ROOT / path) == expected, path
    return p


def prepare_proposal():
    assert not (PREP / "protocol_proposal.json").exists() and not OUT.exists()
    protected = preparation.verify_existing()
    assert read(PREP / "input_verification.json")["failed"] == 0
    ready = read(PREP / "preflight_verification.json")
    assert ready["failed"] == 0 and ready["unit_tests_passed"] == 8
    assert ready["optimizer_steps"] == 0 and ready["parameters_unchanged_after_timing"]
    for name, expected in ready["result_sha256"].items():
        assert sha(PREP / name) == expected, name
    model = DirectionalTFMask()
    sources = [Path(__file__), ROOT / "python_tools/research/directional_tf_mask.py",
        ROOT / "python_tools/prepare_directional_tf_mask.py", ROOT / "python_tools/tests/test_directional_tf_mask.py",
        ROOT / "tools/verification/verify_directional_tf_mask_preparation_20261008.py",
        ROOT / "python_tools/research/candidate_joint_selection_objective_v2.py",
        ROOT / "python_tools/research/candidate_joint_validity_network.py"]
    inputs = [PREP / name for name in ("observed_features.npy", "mask_targets.npy", "spectral_weights.npy",
        "selection_arrays.npz", "input_summary.json", "extraction_protocol.json", "input_verification.json",
        "preflight_verification.json", "timing_estimate.json", "unit_test_results.json")]
    protocol = dict(status="CONCRETE_SINGLE_TRIAL_READY_NOT_AUTHORIZED_NOT_STARTED", date="2026-10-08",
        model_version=VERSION, model_budget=model_budget(model),
        architecture="10->12 shared TF projection; two residual depthwise3x3+pointwise12 causal blocks(dilation time1/2);12->1 mask;mean12+max12+meanmask+maskedcoherence->26->12->1 validity",
        hypothesis="Direction-conditioned TF speech evidence may reject voice leakage/poor steering and attenuate competing sources. Gain is not established",
        observations="Same frozen physical candidate directions; PCM/quiet-reference/mixture-derived WDAS only;67x64x10 per candidate",
        supervision="Preserved302TRAIN+75CAL anchors; amplitude IRM at same estimated beam; selection negatives mask0;14true-short-gray positions ignored",
        output="Neural validity controls every candidate; neural64-band mask directly modifies each selected WDAS speech output",
        from_scratch=True, old_weights_loaded=False, seed=SEED, training_runs_cap=1,
        max_epochs=EPOCHS, batch=BATCH, optimizer_steps_cap=384,
        optimizer=dict(name="AdamW", learning_rate=.001, weight_decay=.0001, gradient_norm_clip=5),
        objective="unchanged four-group BCE weights2/1/1/1 +0.25ranking margin1 +1.0 spectrum-weighted mask MSE, per-window available groups normalized; no loss sweep",
        checkpoint="first strict minimum CAL joint total loss; no checkpoint performance scan",
        thresholds=list(THRESHOLDS), threshold_selection="highest CAL F1 then fewer FP then larger registered threshold",
        decoder="10degree NMS,max8;score descending then candidate index; no permanent physical acceptance branch",
        execution=dict(device="cuda", gpu=ready["gpu"], precision="FP32,TF32disabled,noAMP", torch_threads=2,
            deterministic=True, output_limit_bytes=32*1024**2, cache_limit_bytes=512*1024**2),
        TRAIN_windows=256, CAL_windows=64, new_scene_assets=0, new_TEST_runs=0,
        intended_system_evaluation="Same64windows detection/localization; selected neural masks apply to actual estimated-direction WDAS;same16kWDAS without mask isolates resampling from neural mask gains;6fixed3D+2D maps/audio;background/competing-speech/target-waveform metrics/timing;single existing report",
        acceptance="joint anonymous multisource speech function; no automatic pure-physics win gate; development CAL cannot certify independent generalization",
        deployment="PC research only; no frozen FPGA/RTOS/Qt modifications or board claims",
        authorization="NEW explicit human approval required because prior24epoch budgets were consumed",
        source_sha256={str(p.relative_to(ROOT)): sha(p) for p in sources},
        input_sha256={str(p.relative_to(ROOT)): sha(p) for p in inputs}, protected_sha256=protected)
    save(PREP, "protocol_proposal.json", protocol)
    print(json.dumps(dict(status=protocol["status"], model_budget=protocol["model_budget"],
        measured_timing=read(PREP / "timing_estimate.json"), training_runs=0), ensure_ascii=False))


def run_once(authorization):
    assert authorization.strip(), "Record actual NEW human approval in chat before execution"
    assert not OUT.exists(), "One run only, no overwrite/retry/automatic restart"
    protocol = verify_proposal()
    device = setup(protocol["execution"]["device"])
    assert torch.cuda.get_device_name(0) == protocol["execution"]["gpu"]
    arrays = load_arrays()
    model = DirectionalTFMask().to(device)
    OUT.mkdir()
    save(OUT, "protocol_lock.json", dict(protocol, status="FROZEN_BEFORE_HUMAN_APPROVED_RUN",
        authorization=authorization, proposal_sha256=sha(PREP / "protocol_proposal.json")))
    save(OUT, "training_started.json", dict(authorization=authorization, training_runs=1,
        initial_state_sha256=state_sha(model), max_epochs=EPOCHS, optimizer_steps_cap=384, no_automatic_retry=True))
    optimizer = torch.optim.AdamW(model.parameters(), lr=.001, weight_decay=.0001)
    rng = np.random.default_rng(SEED)

    def cal_losses():
        model.eval(); sums = {}
        indices = arrays["cal_indices"]
        with torch.no_grad():
            for offset in range(0, len(indices), BATCH):
                ids = indices[offset:offset+BATCH]
                values = terms(model, batch(arrays, ids, device))
                for name, value in values.items():
                    sums[name] = sums.get(name, 0.)+float(value)*len(ids)
        return {name: value/len(indices) for name, value in sums.items()}

    initial = cal_losses(); history = []
    best, best_state, best_epoch, steps = float("inf"), None, 0, 0
    started = time.perf_counter()
    for epoch in range(1, EPOCHS+1):
        tick = time.perf_counter(); model.train()
        train_sums, seen, maximum_gradient = {}, 0, 0.
        for offset in range(0, 256, BATCH):
            if offset == 0:
                order = rng.permutation(arrays["train_indices"])
            ids = order[offset:offset+BATCH]
            optimizer.zero_grad(set_to_none=True)
            losses = terms(model, batch(arrays, ids, device))
            assert torch.isfinite(losses["total"])
            losses["total"].backward()
            norm = float(torch.nn.utils.clip_grad_norm_(model.parameters(), 5))
            assert np.isfinite(norm)
            maximum_gradient = max(maximum_gradient, norm)
            optimizer.step(); steps += 1; seen += len(ids)
            for name, value in losses.items():
                train_sums[name] = train_sums.get(name, 0.)+float(value.detach())*len(ids)
        cal = cal_losses()
        assert all(np.isfinite(v) for v in cal.values())
        if cal["total"] < best:
            best, best_epoch = cal["total"], epoch
            best_state = {name: value.detach().cpu().clone() for name, value in model.state_dict().items()}
        row = dict(epoch=epoch, TRAIN_minibatch_losses={k: v/seen for k, v in train_sums.items()}, CAL_losses=cal,
            optimizer_steps=steps, maximum_preclip_gradient_norm=maximum_gradient, best_epoch_so_far=best_epoch,
            elapsed_epoch_seconds=time.perf_counter()-tick)
        history.append(row); print(json.dumps(row), flush=True)
    assert steps == 384 and best_state is not None
    model.load_state_dict(best_state); model.eval()
    logits = np.empty((320, 16), np.float32)
    masks = np.empty((64, 16, 67, 64), np.float32)
    cal_positions = {int(i): j for j, i in enumerate(arrays["cal_indices"])}
    with torch.no_grad():
        for offset in range(0, 320, BATCH):
            ids = np.arange(offset, min(offset+BATCH, 320))
            data = batch(arrays, ids, device)
            output = model(data["features"], data["candidate_valid"])
            logits[ids] = output["validity_logits"].cpu().numpy()
            for j, i in enumerate(ids):
                if int(i) in cal_positions:
                    masks[cal_positions[int(i)]] = torch.sigmoid(output["mask_logits"][j]).cpu().numpy()
    probability = 1/(1+np.exp(-logits.astype(float)))
    torch.save(dict(state_dict=best_state, model_version=VERSION, epoch=best_epoch, cal_loss=best,
        protocol_sha256=sha(OUT / "protocol_lock.json")), OUT / "best_checkpoint.pt")
    np.savez_compressed(OUT / "candidate_predictions.npz", logits=logits, probabilities=probability)
    np.savez_compressed(OUT / "CAL_mask_predictions.npz", scene_indices=arrays["cal_indices"], masks=masks)
    predictions, table = {}, []
    records = read(preparation.previous.base.DATA / "window_records.json")
    scorer = preparation.previous.base.previous
    for threshold in THRESHOLDS:
        rows = []
        for i in arrays["cal_indices"]:
            directions, selected = decode_joint_validity(arrays["directions"][i], probability[i], arrays["candidate_valid"][i], threshold)
            truth, gray, clear = scorer.context(records[i])
            metrics = dict(scorer.score_window(directions, truth, gray), truth_count=len(truth), clear_zero=clear)
            rows.append(dict(index=int(i), scene_id=records[i]["scene_id"], outputs=directions,
                selected_candidates=selected, metrics=metrics))
        predictions[str(threshold)] = rows
        table.append(dict(threshold=threshold, metrics=scorer.aggregate([r["metrics"] for r in rows])))
    chosen = max(table, key=lambda r: (r["metrics"]["f1"], -r["metrics"]["fp"], r["threshold"]))
    save(OUT, "training_history.json", dict(initial_CAL_losses=initial, epochs=history, selected_epoch=best_epoch,
        selected_cal_loss=best, optimizer_steps=steps, elapsed_training_and_export_seconds=time.perf_counter()-started))
    save(OUT, "CAL_all_threshold_predictions.json", predictions)
    save(OUT, "summary.json", dict(status="ONE_AUTHORIZED_MASK_TRAINING_COMPLETED_PENDING_INDEPENDENT_AND_SYSTEM_VERIFICATION",
        training_runs=1, optimizer_steps=steps, selected_epoch=best_epoch, selected_cal_loss=best,
        selected_threshold=chosen["threshold"], threshold_table=table, detection=chosen["metrics"],
        model_budget=protocol["model_budget"], functional_acceptance=False, independent_TEST_runs=0,
        formal_builds=0, board_measurements=0))
    verify_proposal()
    assert sum(p.stat().st_size for p in OUT.iterdir() if p.is_file()) < protocol["execution"]["output_limit_bytes"]


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--prepare-proposal", action="store_true")
    group.add_argument("--run-once", action="store_true")
    parser.add_argument("--authorization", default="")
    args = parser.parse_args()
    if args.prepare_proposal:
        prepare_proposal()
    elif args.run_once:
        run_once(args.authorization)
    else:
        print(json.dumps(dict(status="READ_ONLY_NO_TRAINING_REQUESTED", proposed=(PREP / "protocol_proposal.json").exists(),
                              run_started=OUT.exists(), new_training_runs=0)))
