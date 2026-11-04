"""Inactive one-run detector tuning; frozen current enhancer, no automatic retry."""
import argparse
import shutil
import time
import numpy as np
import torch
import train_multisource_waveform_tuning_one_20261009 as base
from research.multisource_waveform_quality_tuning import WaveformQualityNetwork
from research import multisource_hard_negative_objective as objective

ROOT = base.ROOT
PREP = ROOT/'results/multisource_hard_negative_preparation_20261009'
OUT = ROOT/'results/multisource_hard_negative_one_run_20261009'
SEED, EPOCHS, BATCH, THRESHOLDS = base.SEED, 24, 16, base.THRESHOLDS
read, sha, save, state_sha, setup = base.read, base.sha, base.save, base.state_sha, base.setup
VERSION = base.VERSION  # 推理架构和掩模算子完全相同。
load_arrays, batch, detector_key, metrics = base.load_arrays, base.batch, base.detector_key, base.metrics


def initialized_model(device):
    model = WaveformQualityNetwork().to(device)
    checkpoint = torch.load(base.OUT/'best_checkpoint.pt', map_location=device, weights_only=True)
    model.load_state_dict(checkpoint['state_dict'])
    model.enhancer.requires_grad_(False); model.enhancer.eval()
    return model


def verify_proposal():
    base.verify_proposal()
    protocol = read(PREP/'protocol_proposal.json')
    for section in ('source_sha256', 'input_sha256', 'protected_sha256'):
        for name, expected in protocol[section].items():
            assert sha(ROOT/name) == expected, name
    assert read(PREP/'preflight_verification.json')['failed'] == 0
    return protocol


def run_once(authorization):
    assert authorization.strip() and not OUT.exists(), 'A NEW human budget and unused directory are required'
    protocol = verify_proposal(); arrays = load_arrays(); device = setup()
    model = initialized_model(device); enhancer_hash = state_sha(model.enhancer)
    assert state_sha(model) == read(PREP/'preflight_verification.json')['initial_state_sha256']
    OUT.mkdir(); save(OUT, 'protocol_lock.json', dict(protocol, authorization=authorization))
    save(OUT, 'training_started.json', dict(training_runs=1, max_epochs=24, seed=SEED,
        authorization=authorization, initial_state_sha256=state_sha(model), frozen_enhancer_sha256=enhancer_hash))
    train, cal = arrays['train_indices'], arrays['cal_indices']
    initial_logits = base.prior.scores(model, arrays, cal, device)
    chosen, _, _ = metrics(arrays, cal, initial_logits)
    save(OUT, 'initial_checkpoint_evaluation.json', dict(epoch=0, detection=chosen))
    best_key = detector_key(chosen, 0); best_epoch = 0
    best_state = {k: v.detach().cpu().clone() for k, v in model.detector.state_dict().items()}
    optimizer = torch.optim.AdamW(model.detector.parameters(), lr=.0003, weight_decay=.0001)
    rng = np.random.default_rng(SEED); history = []; steps = 0; started = time.perf_counter()
    for epoch in range(1, 25):
        model.detector.train()
        for ids in np.array_split(rng.permutation(train), 16):
            b = batch(arrays, ids, device, False)
            optimizer.zero_grad(set_to_none=True)
            logits = model.detector(b['detection_features'], b['directions'], b['candidate_valid'])
            loss = objective.terms(logits, b['labels'], b['candidate_valid'], b['group_masks'], b['ranking_pairs'])['total']
            assert torch.isfinite(loss)
            loss.backward(); torch.nn.utils.clip_grad_norm_(model.detector.parameters(), 5)
            optimizer.step(); steps += 1
            assert all(p.grad is None for p in model.enhancer.parameters())
        chosen, _, _ = metrics(arrays, cal, base.prior.scores(model, arrays, cal, device))
        key = detector_key(chosen, epoch)
        if key > best_key:
            best_key = key; best_epoch = epoch
            best_state = {k: v.detach().cpu().clone() for k, v in model.detector.state_dict().items()}
        row = dict(epoch=epoch, CAL_detection=chosen, detector_steps=steps, enhancer_steps=0,
            selected_detector_epoch=best_epoch, elapsed_seconds=time.perf_counter()-started)
        history.append(row); print(row, flush=True)
        assert state_sha(model.enhancer) == enhancer_hash
    model.detector.load_state_dict(best_state); model.eval()
    logits = base.prior.scores(model, arrays, np.arange(320), device)
    chosen, table, predictions = metrics(arrays, cal, logits[cal])
    original = torch.load(base.OUT/'best_checkpoint.pt', map_location='cpu', weights_only=True)
    torch.save(dict(state_dict={k: v.detach().cpu() for k, v in model.state_dict().items()}, model_version=VERSION,
        objective_version=objective.VERSION, detector_epoch=best_epoch, enhancer_epoch=original['enhancer_epoch'],
        epoch=best_epoch, frozen_enhancer_source_checkpoint_sha256=sha(base.OUT/'best_checkpoint.pt'),
        protocol_sha256=sha(OUT/'protocol_lock.json')), OUT/'best_checkpoint.pt')
    np.savez_compressed(OUT/'candidate_predictions.npz', logits=logits, probabilities=1/(1+np.exp(-logits.astype(float))))
    # 整个增强器冻结，因此全部开发校准/候选掩模不变。
    shutil.copy2(base.OUT/'CAL_mask_predictions.npz', OUT/'CAL_mask_predictions.npz')
    save(OUT, 'CAL_all_threshold_predictions.json', predictions)
    save(OUT, 'training_history.json', dict(epochs=history, detector_steps=steps, enhancer_steps=0,
        selected_detector_epoch=best_epoch, frozen_enhancer_sha256=enhancer_hash,
        elapsed_training_and_export_seconds=time.perf_counter()-started))
    save(OUT, 'summary.json', dict(status='DETECTOR_TUNING_COMPLETED_PENDING_FULL_CHAIN_VALIDATION', training_runs=1,
        selected_epoch=best_epoch, selected_enhancer_epoch=original['enhancer_epoch'], selected_threshold=chosen['threshold'],
        detection=chosen['metrics'], threshold_table=table, frozen_enhancer_sha256=enhancer_hash,
        selected_enhancement_anchor_evaluation=read(base.OUT/'summary.json')['selected_enhancement_anchor_evaluation'],
        functional_acceptance=False, PC_frozen=False, FPGA_frozen=False, new_TEST_runs=0, formal_builds=0, board_measurements=0))
    assert state_sha(model.enhancer) == enhancer_hash; verify_proposal()
    print(dict(stage='ONE_DETECTOR_TUNING_DONE', detection=chosen['metrics'], detector_epoch=best_epoch, enhancer_updates=0), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--run-once', action='store_true')
    parser.add_argument('--authorization', required=True); args = parser.parse_args()
    if args.run_once:
        run_once(args.authorization)
