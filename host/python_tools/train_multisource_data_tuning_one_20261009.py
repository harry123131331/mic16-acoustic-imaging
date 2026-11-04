"""One inactive data-diversity tune; explicit new budget required to run."""
import argparse
import math
import shutil
import time
import numpy as np
import torch
import train_multisource_hard_negative_one_20261009 as previous
from research.multisource_waveform_quality_tuning import WaveformQualityNetwork
from research import multisource_hard_negative_objective as objective

ROOT = previous.ROOT
PREP = ROOT/'results/multisource_data_tuning_preparation_20261009'
OUT = ROOT/'results/multisource_data_tuning_one_run_20261009'
SEED, EPOCHS, BATCH, THRESHOLDS = 20261009, 24, 16, previous.THRESHOLDS
base = previous.base
read, sha, save, state_sha = previous.read, previous.sha, previous.save, previous.state_sha
VERSION = previous.VERSION
batch, metrics, detector_key = previous.batch, previous.metrics, previous.detector_key


def setup(device='cuda'):
    d = previous.setup(device)
    np.random.seed(SEED); torch.manual_seed(SEED)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(SEED)
    return d


def load_arrays():
    arrays = previous.load_arrays()
    with np.load(PREP/'TRAIN_variants.npz') as z:
        for key in ('detection_features','directions','candidate_valid','labels','group_masks','ranking_pairs'):
            arrays[key] = np.concatenate((np.asarray(arrays[key]), z[key]), axis=0)
    arrays['records'] = list(arrays['records']) + read(PREP/'window_records.json')
    arrays['train_indices'] = np.r_[arrays['train_indices'], np.arange(320,448)]
    assert len(arrays['train_indices']) == 384 and len(arrays['cal_indices']) == 64
    assert not set(arrays['train_indices']) & set(arrays['cal_indices'])
    return arrays


def initialized_model(device):
    model = WaveformQualityNetwork().to(device)
    checkpoint = torch.load(previous.OUT/'best_checkpoint.pt', map_location=device, weights_only=True)
    model.load_state_dict(checkpoint['state_dict'])
    model.enhancer.requires_grad_(False); model.enhancer.eval()
    return model


def verify_proposal(require_preflight=True):
    previous.verify_proposal()
    protocol = read(PREP/'protocol_proposal.json')
    for section in ('source_sha256','input_sha256','protected_sha256'):
        for name, expected in protocol[section].items():
            assert sha(ROOT/name) == expected, name
    preparation = read(PREP/'preparation_result.json')
    assert preparation['failed'] == 0
    assert sha(PREP/'TRAIN_variants.npz') == preparation['variants_sha256']
    assert sha(PREP/'window_records.json') == preparation['records_sha256']
    assert sha(PREP/'scene_plan.json') == preparation['plan_sha256']
    if require_preflight:
        assert read(PREP/'preflight_verification.json')['failed'] == 0
    return protocol


def run_once(authorization):
    assert authorization.strip() and not OUT.exists(), 'New human authorization and an unused directory required'
    protocol = verify_proposal(); arrays = load_arrays(); device = setup()
    model = initialized_model(device); enhancer_hash = state_sha(model.enhancer)
    preflight = read(PREP/'preflight_verification.json')
    assert state_sha(model) == preflight['initial_state_sha256']
    OUT.mkdir(); save(OUT,'protocol_lock.json',dict(protocol,authorization=authorization,
        authorization_required_because='Prior user instruction: no automatic extra training; previous one-run budget spent.'))
    save(OUT,'training_started.json',dict(training_runs=1,max_epochs=EPOCHS,seed=SEED,
        authorization=authorization,initial_state_sha256=state_sha(model),frozen_enhancer_sha256=enhancer_hash))
    train, cal = arrays['train_indices'], arrays['cal_indices']
    chosen, _, _ = metrics(arrays,cal,base.prior.scores(model,arrays,cal,device))
    save(OUT,'initial_checkpoint_evaluation.json',dict(epoch=0,detection=chosen))
    best_key = detector_key(chosen,0); best_epoch = 0
    best_state = {k:v.detach().cpu().clone() for k,v in model.detector.state_dict().items()}
    optimizer = torch.optim.AdamW(model.detector.parameters(),lr=.0003,weight_decay=.0001)
    rng = np.random.default_rng(SEED); history=[]; steps=0; started=time.perf_counter()
    for epoch in range(1,EPOCHS+1):
        model.detector.train()
        ordered = rng.permutation(train)
        for start in range(0,len(ordered),BATCH):
            b = batch(arrays,ordered[start:start+BATCH],device,False)
            optimizer.zero_grad(set_to_none=True)
            logits = model.detector(b['detection_features'],b['directions'],b['candidate_valid'])
            loss = objective.terms(logits,b['labels'],b['candidate_valid'],b['group_masks'],b['ranking_pairs'])['total']
            assert torch.isfinite(loss)
            loss.backward(); torch.nn.utils.clip_grad_norm_(model.detector.parameters(),5)
            optimizer.step(); steps += 1
            assert all(p.grad is None for p in model.enhancer.parameters())
        chosen,_,_ = metrics(arrays,cal,base.prior.scores(model,arrays,cal,device))
        key = detector_key(chosen,epoch)
        if key > best_key:
            best_key = key; best_epoch = epoch
            best_state = {k:v.detach().cpu().clone() for k,v in model.detector.state_dict().items()}
        row = dict(epoch=epoch,CAL_detection=chosen,detector_steps=steps,enhancer_steps=0,
                   selected_detector_epoch=best_epoch,elapsed_seconds=time.perf_counter()-started)
        history.append(row); print(row,flush=True)
        assert state_sha(model.enhancer) == enhancer_hash
    assert steps == EPOCHS*math.ceil(len(train)/BATCH) == 576
    model.detector.load_state_dict(best_state); model.eval()
    logits = base.prior.scores(model,arrays,np.arange(448),device)
    chosen,table,predictions = metrics(arrays,cal,logits[cal])
    original = torch.load(previous.OUT/'best_checkpoint.pt',map_location='cpu',weights_only=True)
    if best_epoch == 0:
        shutil.copy2(previous.OUT/'best_checkpoint.pt',OUT/'best_checkpoint.pt')
    else:
        torch.save(dict(state_dict={k:v.detach().cpu() for k,v in model.state_dict().items()},
            model_version=VERSION,objective_version=objective.VERSION,detector_epoch=best_epoch,
            enhancer_epoch=original['enhancer_epoch'],epoch=best_epoch,
            frozen_enhancer_source_checkpoint_sha256=sha(previous.OUT/'best_checkpoint.pt'),
            protocol_sha256=sha(OUT/'protocol_lock.json')),OUT/'best_checkpoint.pt')
    np.savez_compressed(OUT/'candidate_predictions.npz',logits=logits,probabilities=1/(1+np.exp(-logits.astype(float))))
    shutil.copy2(previous.OUT/'CAL_mask_predictions.npz',OUT/'CAL_mask_predictions.npz')
    save(OUT,'CAL_all_threshold_predictions.json',predictions)
    save(OUT,'training_history.json',dict(epochs=history,detector_steps=steps,enhancer_steps=0,
        selected_detector_epoch=best_epoch,frozen_enhancer_sha256=enhancer_hash,
        elapsed_training_and_export_seconds=time.perf_counter()-started))
    save(OUT,'summary.json',dict(status='ONE_DATA_DIVERSITY_TUNE_DONE_PENDING_VALIDATION',training_runs=1,
        selected_epoch=best_epoch,selected_enhancer_epoch=original['enhancer_epoch'],
        selected_threshold=chosen['threshold'],detection=chosen['metrics'],threshold_table=table,
        kept_initial_checkpoint=best_epoch==0,TRAIN_windows=384,CAL_windows=64,
        frozen_enhancer_sha256=enhancer_hash,functional_acceptance=False,PC_frozen=False,FPGA_frozen=False,
        new_TEST_runs=0,formal_builds=0,board_measurements=0))
    verify_proposal()
    print(dict(stage='ONE_DATA_TUNE_DONE',detection=chosen['metrics'],selected_epoch=best_epoch),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-once',action='store_true',required=True)
    parser.add_argument('--authorization',required=True)
    args = parser.parse_args()
    if args.run_once:
        run_once(args.authorization)
