"""Inactive, bounded ordinary-scene repair. New explicit human budget required."""
import argparse
import time
import shutil
import numpy as np
import torch
import train_multisource_utterance_tuning_one_20261009 as data
from research import multisource_window_error_objective as objective
from research import multisource_basic_release_selection as selection
from research.multisource_waveform_quality_tuning import model_budget

ROOT = data.ROOT
PREP = ROOT/'results/multisource_basic_repair_preparation_20261009'
OUT = ROOT/'results/multisource_basic_repair_one_run_20261009'
SEED, EPOCHS, BATCH = 2026100909, 8, 16
read, save, sha, state_sha = data.read, data.save, data.sha, data.state_sha


def verify_proposal():
    data.verify_proposal()
    p = read(PREP/'protocol_proposal.json')
    for section in ('source_sha256', 'input_sha256', 'protected_sha256'):
        for name, expected in p[section].items():
            assert sha(ROOT/name) == expected, name
    assert read(PREP/'preflight_verification.json')['failed'] == 0
    return p


def initialized_model(device):
    return data.initialized_model(device)


def evaluate(model, arrays, device):
    ids = arrays['cal_indices']
    logits = data.base.prior.scores(model, arrays, ids, device)
    return selection.evaluation(arrays, ids, logits, data.base.prior.metrics)


def run_once(authorization):
    assert authorization.strip() and not OUT.exists(), 'A new human budget and unused output directory are required'
    protocol = verify_proposal(); arrays = data.load_arrays(); device = data.setup()
    np.random.seed(SEED); torch.manual_seed(SEED)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(SEED)
    model = initialized_model(device)
    initial = state_sha(model); enhancer_sha = state_sha(model.enhancer)
    assert initial == read(PREP/'preflight_verification.json')['initial_state_sha256']
    baseline = evaluate(model, arrays, device)
    assert baseline == read(PREP/'baseline_evaluation.json')
    OUT.mkdir()
    save(OUT, 'protocol_lock.json', dict(protocol, authorization=authorization))
    save(OUT, 'training_started.json', dict(training_runs=1, max_epochs=EPOCHS,
        max_detector_updates=224, enhancer_updates=0, authorization=authorization,
        seed=SEED, initial_state_sha256=initial, frozen_enhancer_sha256=enhancer_sha))
    optimizer = torch.optim.AdamW(model.detector.parameters(), lr=.0003, weight_decay=.0001)
    best_state = {k:v.detach().cpu().clone() for k,v in model.detector.state_dict().items()}
    best_epoch = steps = 0; best_row = baseline
    rng = np.random.default_rng(SEED); history = []; tick = time.perf_counter()
    for epoch in range(1,EPOCHS+1):
        model.detector.train(); model.enhancer.eval()
        ordered = rng.permutation(arrays['train_indices'])
        for start in range(0,len(ordered),BATCH):
            b = data.batch(arrays, ordered[start:start+BATCH], device, False)
            optimizer.zero_grad(set_to_none=True)
            logits = model.detector(b['detection_features'], b['directions'], b['candidate_valid'])
            loss = objective.terms(logits,b['labels'],b['candidate_valid'],b['group_masks'],b['ranking_pairs'])['total']
            assert torch.isfinite(loss)
            loss.backward(); torch.nn.utils.clip_grad_norm_(model.detector.parameters(),5)
            optimizer.step(); steps += 1
            assert all(p.grad is None for p in model.enhancer.parameters())
        row = evaluate(model, arrays, device); passes = selection.eligible(row,baseline)
        if passes and (best_epoch == 0 or selection.key(row,epoch) > selection.key(best_row,best_epoch)):
            best_state = {k:v.detach().cpu().clone() for k,v in model.detector.state_dict().items()}
            best_epoch, best_row = epoch, row
        assert state_sha(model.enhancer) == enhancer_sha
        item = dict(epoch=epoch, CAL=row, passes_release_guard=passes, selected_epoch=best_epoch,
                    detector_steps=steps, enhancer_steps=0, elapsed_seconds=time.perf_counter()-tick)
        history.append(item); print(item,flush=True)
    model.detector.load_state_dict(best_state); model.eval()
    assert steps == 224
    logits = data.base.prior.scores(model, arrays, np.arange(512), device)
    _, table, predictions = data.base.prior.metrics(arrays, arrays['cal_indices'], logits[arrays['cal_indices']])
    original = torch.load(data.previous.OUT/'best_checkpoint.pt',map_location='cpu',weights_only=True)
    torch.save(dict(state_dict={k:v.detach().cpu() for k,v in model.state_dict().items()},
        model_version=data.VERSION, objective_version=objective.VERSION, selection_version=selection.VERSION,
        detector_epoch=best_epoch, enhancer_epoch=original['enhancer_epoch'], epoch=best_epoch,
        source_detector_epoch=original['detector_epoch'], selected_threshold=.65,
        protocol_sha256=sha(OUT/'protocol_lock.json')),OUT/'best_checkpoint.pt')
    np.savez_compressed(OUT/'candidate_predictions.npz', logits=logits,
                        probabilities=1/(1+np.exp(-logits.astype(float))))
    shutil.copy2(data.previous.OUT/'CAL_mask_predictions.npz',OUT/'CAL_mask_predictions.npz')
    save(OUT,'CAL_all_threshold_predictions.json',predictions)
    save(OUT,'training_history.json',dict(epochs=history, detector_steps=steps, enhancer_steps=0,
        selected_detector_epoch=best_epoch, elapsed_training_and_export_seconds=time.perf_counter()-tick))
    save(OUT,'summary.json',dict(status='REPAIR_DONE_PENDING_PCM_REPLAY' if best_epoch else 'NO_GUARDED_IMPROVEMENT_BASELINE_RETAINED',
        training_runs=1, selected_epoch=best_epoch, source_detector_epoch=14,
        selected_enhancer_epoch=original['enhancer_epoch'],selected_threshold=.65,
        detection=best_row['metrics'],basic_evaluation=best_row,baseline_evaluation=baseline,
        diagnostic_threshold_table=table,diagnostic_thresholds_not_used_for_selection=True,
        model_budget=model_budget(model),
        new_TEST_runs=0, functional_acceptance=False, PC_frozen=False, FPGA_frozen=False,
        formal_builds=0, board_measurements=0, automatic_retry=False))
    verify_proposal()


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--run-once',action='store_true'); p.add_argument('--authorization',required=True)
    args = p.parse_args()
    if args.run_once:
        run_once(args.authorization)
