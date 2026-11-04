"""Concrete one-run trainer; inactive until a NEW explicit human budget approval."""
import argparse
import time
import numpy as np
import torch
import train_directional_tf_mask_one as prior
import prepare_directional_target_fidelity as preparation
from research.directional_target_fidelity import DirectionalTFMask, model_budget, objective, VERSION
from research.candidate_joint_validity_network import decode_joint_validity

ROOT, PREP = prior.ROOT, preparation.OUT
OUT = ROOT / 'results/directional_target_fidelity_one_run_20261009'
SEED, EPOCHS, BATCH, THRESHOLDS = prior.SEED, 24, 16, (.5, .65, .8)
read, sha, save, setup, state_sha = prior.read, prior.sha, prior.save, prior.setup, prior.state_sha
SELECTION_KEYS = ('selection_total', 'weighted_group_bce', 'source_ranking')
ENHANCEMENT_KEYS = ('spectral_signal_approximation', 'target_component_distortion', 'enhancement_total')


def load_arrays():
    base = prior.load_arrays()
    arrays = {k: v for k, v in base.items() if k not in ('targets', 'weights')}
    arrays['coefficients'] = np.load(PREP / 'positive_reconstruction_coefficients.npy', mmap_mode='r')
    with np.load(PREP / 'anchor_index_map.npz') as data:
        arrays['anchor_index_map'] = data['index_map'].copy()
    assert np.array_equal(arrays['anchor_index_map'] >= 0, arrays['group_masks'][:, 0])
    assert arrays['coefficients'].shape == (377, 67, 64, 3)
    return arrays


def batch(arrays, ids, device):
    ids = np.asarray(ids)
    b = dict(features=torch.from_numpy(np.array(arrays['features'][ids], dtype=np.float32, copy=True)).to(device))
    for name in ('labels', 'candidate_valid', 'group_masks', 'ranking_pairs'):
        b[name] = torch.from_numpy(arrays[name][ids].copy()).to(device)
    mapping = arrays['anchor_index_map'][ids]; selected = mapping >= 0
    coefficients = np.zeros((len(ids), 16, 67, 64, 3), np.float32)
    coefficients[selected] = arrays['coefficients'][mapping[selected]]
    b['coefficients'] = torch.from_numpy(coefficients).to(device)
    return b


def terms(model, b):
    out = model(b['features'], b['candidate_valid'])
    return objective(out, b['labels'], b['candidate_valid'], b['group_masks'], b['ranking_pairs'], b['coefficients'])


def evaluation_losses(model, arrays, indices, device):
    sums = {k: 0. for k in (*SELECTION_KEYS, *ENHANCEMENT_KEYS)}
    n_selection, n_enhancement = 0, 0
    model.eval()
    with torch.no_grad():
        for offset in range(0, len(indices), BATCH):
            b = batch(arrays, indices[offset:offset+BATCH], device)
            values = terms(model, b)
            ns = int((b['candidate_valid'] & (b['labels'][..., 1] >= 0)).any(1).sum())
            ne = int(b['group_masks'][:, 0].any(1).sum())
            n_selection += ns; n_enhancement += ne
            for key in SELECTION_KEYS:
                sums[key] += float(values[key])*ns
            for key in ENHANCEMENT_KEYS:
                sums[key] += float(values[key])*ne
    result = {key: sums[key]/n_selection if n_selection else 0. for key in SELECTION_KEYS}
    result.update({key: sums[key]/n_enhancement if n_enhancement else 0. for key in ENHANCEMENT_KEYS})
    result['total'] = result['selection_total']+result['enhancement_total']
    return result


def verify_proposal():
    proposal = read(PREP / 'protocol_proposal.json')
    for section in ('source_sha256', 'input_sha256', 'protected_sha256'):
        for path, expected in proposal[section].items():
            assert sha(ROOT / path) == expected, path
    return proposal


def prepare_proposal():
    assert not (PREP / 'protocol_proposal.json').exists() and not OUT.exists()
    protected = preparation.verify_prior()
    ready = read(PREP / 'preflight_verification.json')
    assert ready['failed'] == 0 and ready['unit_tests_passed'] == 7
    assert ready['optimizer_steps'] == 0 and ready['parameters_unchanged_after_checks']
    for name, expected in ready['result_sha256'].items():
        assert sha(PREP / name) == expected, name
    sources = [ROOT / p for p in ('python_tools/train_directional_target_fidelity_one.py',
        'python_tools/prepare_directional_target_fidelity.py', 'python_tools/research/directional_target_fidelity.py',
        'python_tools/tests/test_directional_target_fidelity.py',
        'tools/verification/verify_directional_target_fidelity_preparation_20261009.py',
        'python_tools/train_directional_tf_mask_one.py', 'python_tools/research/directional_tf_mask.py',
        'python_tools/research/candidate_joint_selection_objective_v2.py', 'python_tools/research/candidate_joint_validity_network.py')]
    inputs = [PREP / n for n in ('extraction_protocol.json', 'positive_reconstruction_coefficients.npy',
        'anchor_index_map.npz', 'input_summary.json', 'input_verification.json', 'preflight_verification.json',
        'unit_test_results.json', 'timing_estimate.json')]
    inputs += [prior.PREP / 'observed_features.npy', prior.PREP / 'selection_arrays.npz']
    proposal = dict(status='TARGET_FIDELITY_SINGLE_RUN_READY_NOT_AUTHORIZED_NOT_STARTED', date='2026-10-09',
        objective_version=VERSION, inference_model_version=prior.VERSION,
        model_budget=model_budget(DirectionalTFMask()), architecture_unchanged=True,
        hypothesis='Positive-only spectral reconstruction and explicit target-component preservation may reduce global muting; direction discrimination remains an unproven joint-training outcome',
        objectives='Original four-group2/1/1/1 BCE+.25ranking margin1; enhancement=sum|mY-S|²/E_S +.25sum|(1-m)S|²/E_S on positive anchors only, multiplier1.0',
        no_zero_mask_negative_supervision=True, shared_encoder_and_validity_mask_statistics_still_coupled=True,
        output='Neural validity controls all candidate acceptance; actual accepted WDAS spectra use predicted neural masks',
        from_scratch=True, old_weights_loaded=False, seed=SEED, same_initial_seed_as_previous=True,
        training_runs_cap=1, max_epochs=EPOCHS, optimizer_steps_cap=384, batch=BATCH,
        optimizer=dict(name='AdamW', learning_rate=.001, weight_decay=.0001, gradient_norm_clip=5),
        checkpoint='first strict minimum CAL joint loss; CAL enhancement averaged by positive-window counts, selection by supervised-window counts',
        thresholds=list(THRESHOLDS), threshold_selection='highest CAL F1 then fewer FP then larger registered threshold',
        decoder='10deg NMS max8, descending score then candidate index; unchanged',
        execution=dict(device='cuda', gpu=ready['gpu'], precision='FP32,TF32disabled,noAMP', torch_threads=2,
            deterministic=True, output_limit_bytes=32*1024**2, preparation_limit_bytes=32*1024**2),
        TRAIN_windows=256, CAL_windows=64, preserved_TRAIN_anchors=302, preserved_CAL_anchors=75,
        new_scene_assets=0, new_TEST_runs=0,
        intended_system_evaluation='Same64 full detection/localization/mask enhancement; same16k estimated-direction WDAS baseline; six fixed3D+2D maps, actual audio, target/background/other speech/SI-SDR/timing/cost; update single Chinese report',
        acceptance='Functional detection/localization/enhancement; pure-physics v1 is a diagnostic reference, not automatic win gate',
        deployment='PC research only, no frozen FPGA/RTOS/Qt changes or board claims',
        authorization='NEW explicit human approval is required; previous24epoch budgets are consumed',
        source_sha256={str(p.relative_to(ROOT)): sha(p) for p in sources},
        input_sha256={str(p.relative_to(ROOT)): sha(p) for p in inputs}, protected_sha256=protected)
    save(PREP, 'protocol_proposal.json', proposal)
    print(dict(status=proposal['status'], new_training_runs=0, model_budget=proposal['model_budget']), flush=True)


def run_once(authorization):
    assert authorization.strip(), 'Requires actual NEW human budget approval in chat'
    assert not OUT.exists(), 'One run only; no overwrite/retry/automatic restart'
    protocol = verify_proposal(); preparation.verify_prior()
    device = setup('cuda')
    assert torch.cuda.get_device_name(0) == protocol['execution']['gpu']
    arrays = load_arrays(); model = DirectionalTFMask().to(device)
    OUT.mkdir()
    save(OUT, 'protocol_lock.json', dict(protocol, status='FROZEN_BEFORE_NEW_HUMAN_APPROVED_RUN',
        authorization=authorization, proposal_sha256=sha(PREP / 'protocol_proposal.json')))
    save(OUT, 'training_started.json', dict(authorization=authorization, training_runs=1, max_epochs=24,
        optimizer_steps_cap=384, initial_state_sha256=state_sha(model), no_automatic_retry=True))
    optimizer = torch.optim.AdamW(model.parameters(), lr=.001, weight_decay=.0001)
    rng = np.random.default_rng(SEED)
    initial = evaluation_losses(model, arrays, arrays['cal_indices'], device)
    started = time.perf_counter(); history = []
    best, best_epoch, best_state, steps = float('inf'), 0, None, 0
    for epoch in range(1, EPOCHS+1):
        tick = time.perf_counter(); model.train(); sums = {}; seen = 0; gradient_max = 0.
        order = rng.permutation(arrays['train_indices'])
        for offset in range(0, 256, BATCH):
            ids = order[offset:offset+BATCH]; b = batch(arrays, ids, device)
            optimizer.zero_grad(set_to_none=True); losses = terms(model, b)
            assert all(torch.isfinite(v) for v in losses.values())
            losses['total'].backward()
            norm = float(torch.nn.utils.clip_grad_norm_(model.parameters(), 5))
            assert np.isfinite(norm); gradient_max = max(gradient_max, norm)
            optimizer.step(); steps += 1; seen += len(ids)
            for k, v in losses.items():
                sums[k] = sums.get(k, 0.)+float(v.detach())*len(ids)
        cal = evaluation_losses(model, arrays, arrays['cal_indices'], device)
        assert all(np.isfinite(v) for v in cal.values())
        if cal['total'] < best:
            best, best_epoch = cal['total'], epoch
            best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}
        row = dict(epoch=epoch, TRAIN_minibatch_losses={k: v/seen for k, v in sums.items()}, CAL_losses=cal,
            optimizer_steps=steps, maximum_preclip_gradient_norm=gradient_max, best_epoch_so_far=best_epoch,
            elapsed_epoch_seconds=time.perf_counter()-tick)
        history.append(row); print(row, flush=True)
    assert steps == 384 and best_state is not None
    model.load_state_dict(best_state); model.eval()
    logits = np.empty((320, 16), np.float32); masks = np.empty((64, 16, 67, 64), np.float32)
    positions = {int(i): j for j, i in enumerate(arrays['cal_indices'])}
    with torch.no_grad():
        for offset in range(0, 320, BATCH):
            ids = np.arange(offset, offset+BATCH); b = batch(arrays, ids, device)
            out = model(b['features'], b['candidate_valid']); logits[ids] = out['validity_logits'].cpu().numpy()
            for j, i in enumerate(ids):
                if int(i) in positions:
                    masks[positions[int(i)]] = torch.sigmoid(out['mask_logits'][j]).cpu().numpy()
    probability = 1/(1+np.exp(-logits.astype(float)))
    torch.save(dict(state_dict=best_state, objective_version=VERSION, model_version=prior.VERSION, epoch=best_epoch,
        cal_loss=best, protocol_sha256=sha(OUT / 'protocol_lock.json')), OUT / 'best_checkpoint.pt')
    np.savez_compressed(OUT / 'candidate_predictions.npz', logits=logits, probabilities=probability)
    np.savez_compressed(OUT / 'CAL_mask_predictions.npz', scene_indices=arrays['cal_indices'], masks=masks)
    predictions, table = {}, []
    records = read(prior.preparation.previous.base.DATA / 'window_records.json')
    scorer = prior.preparation.previous.base.previous
    for threshold in THRESHOLDS:
        rows = []
        for value in arrays['cal_indices']:
            i = int(value)
            points, selected = decode_joint_validity(arrays['directions'][i], probability[i], arrays['candidate_valid'][i], threshold)
            truth, gray, clear = scorer.context(records[i])
            metrics = dict(scorer.score_window(points, truth, gray), truth_count=len(truth), clear_zero=clear)
            rows.append(dict(index=i, scene_id=records[i]['scene_id'], outputs=points, selected_candidates=selected, metrics=metrics))
        predictions[str(threshold)] = rows
        table.append(dict(threshold=threshold, metrics=scorer.aggregate([r['metrics'] for r in rows])))
    chosen = max(table, key=lambda r: (r['metrics']['f1'], -r['metrics']['fp'], r['threshold']))
    save(OUT, 'training_history.json', dict(initial_CAL_losses=initial, epochs=history, selected_epoch=best_epoch,
        selected_cal_loss=best, optimizer_steps=384, elapsed_training_and_export_seconds=time.perf_counter()-started))
    save(OUT, 'CAL_all_threshold_predictions.json', predictions)
    save(OUT, 'summary.json', dict(status='ONE_FIDELITY_RUN_COMPLETED_PENDING_INDEPENDENT_AND_SYSTEM_VERIFICATION',
        training_runs=1, optimizer_steps=384, selected_epoch=best_epoch, selected_cal_loss=best,
        selected_threshold=chosen['threshold'], threshold_table=table, detection=chosen['metrics'],
        model_budget=protocol['model_budget'], functional_acceptance=False, new_TEST_runs=0, formal_builds=0, board_measurements=0))
    verify_proposal(); preparation.verify_prior()
    assert sum(p.stat().st_size for p in OUT.iterdir() if p.is_file()) < 32*1024**2


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); group = parser.add_mutually_exclusive_group()
    group.add_argument('--prepare-proposal', action='store_true'); group.add_argument('--run-once', action='store_true')
    parser.add_argument('--authorization', default=''); args = parser.parse_args()
    if args.prepare_proposal:
        prepare_proposal()
    elif args.run_once:
        run_once(args.authorization)
    else:
        print(dict(status='READ_ONLY_NO_NEW_TRAINING_REQUESTED', proposed=(PREP / 'protocol_proposal.json').exists(),
                   run_started=OUT.exists(), new_training_runs=0))
