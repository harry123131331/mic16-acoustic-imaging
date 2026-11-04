"""One newly authorized bounded split-model training job, no automatic retry."""
import os
for name in ('OMP_NUM_THREADS', 'MKL_NUM_THREADS', 'OPENBLAS_NUM_THREADS'):
    os.environ[name] = '2'
os.environ['CUBLAS_WORKSPACE_CONFIG'] = ':4096:8'
from pathlib import Path
import argparse
import time
import numpy as np
import torch
import train_directional_target_fidelity_one as prior
import train_candidate_joint_selection_v2_one as selection
from research.multisource_split_network import SplitMultisourceNetwork, detection_observations, enhancement_terms, model_budget, VERSION
from research.candidate_joint_selection_objective_v2 import selection_objective_terms
from research.candidate_joint_validity_network import decode_joint_validity

ROOT = prior.ROOT
PREP = ROOT / 'results/multisource_split_preparation_20261009'
OUT = ROOT / 'results/multisource_split_one_run_20261009'
SEED, EPOCHS, BATCH, THRESHOLDS = 20261008, 24, 16, (.5, .65, .8)
read, sha, save, state_sha = prior.read, prior.sha, prior.save, prior.state_sha


def load_arrays():
    arrays = prior.load_arrays()
    base = selection.load_training_arrays()
    for old, new in (('valid', 'candidate_valid'), ('groups', 'group_masks'), ('directions', 'directions'), ('labels', 'labels')):
        assert np.array_equal(base[old], arrays[new]), (old, new)
    arrays['physical_features'] = base['features']
    arrays['records'] = base['records']
    arrays['detection_features'] = np.load(PREP/'detection_features.npy', mmap_mode='r') if (PREP/'detection_features.npy').exists() else None
    return arrays


def setup(device='cuda'):
    d = prior.setup(device)
    assert prior.SEED == SEED
    return d


def batch(arrays, ids, device, include_positive=True):
    ids = np.asarray(ids)
    result = {key:torch.from_numpy(np.array(arrays[key][ids], copy=True)).to(device)
        for key in ('detection_features', 'directions', 'candidate_valid', 'labels', 'group_masks', 'ranking_pairs')}
    if include_positive:
        positions, candidates = np.nonzero(arrays['group_masks'][ids, 0])
        anchor_ids = arrays['anchor_index_map'][ids[positions], candidates]
        result.update(positive_features=torch.from_numpy(np.array(arrays['features'][ids[positions], candidates], dtype=np.float32, copy=True)).to(device),
            coefficients=torch.from_numpy(np.array(arrays['coefficients'][anchor_ids], copy=True)).to(device),
            scene_positions=torch.from_numpy(positions).to(device))
    return result


def detection_terms(model, b):
    logits = model.detector(b['detection_features'], b['directions'], b['candidate_valid'])
    return selection_objective_terms(logits, b['labels'], b['candidate_valid'], b['group_masks'], b['ranking_pairs'])


def scores(model, arrays, ids, device):
    model.eval(); values = []
    with torch.no_grad():
        for start in range(0, len(ids), BATCH):
            b = batch(arrays, ids[start:start+BATCH], device, False)
            values.append(model.detector(b['detection_features'], b['directions'], b['candidate_valid']).cpu().numpy())
    return np.concatenate(values)


def metrics(arrays, ids, logits):
    probabilities = 1/(1+np.exp(-logits.astype(float)))
    tables, predictions = [], {}
    for threshold in THRESHOLDS:
        entries = []
        for j, i in enumerate(ids):
            points, selected = decode_joint_validity(arrays['directions'][i], probabilities[j], arrays['candidate_valid'][i], threshold)
            truth, gray, clear = selection.previous.context(arrays['records'][i])
            row = dict(selection.previous.score_window(points, truth, gray), truth_count=len(truth), clear_zero=clear)
            entries.append(dict(index=int(i), scene_id=arrays['records'][i]['scene_id'], outputs=points, selected_candidates=selected, metrics=row))
        predictions[str(threshold)] = entries
        tables.append(dict(threshold=threshold, metrics=selection.previous.aggregate([e['metrics'] for e in entries])))
    best = max(tables, key=lambda r:(r['metrics']['f1'], -r['metrics']['fp'], r['threshold']))
    return best, tables, predictions


def enhancement_evaluation(model, arrays, ids, device):
    values = {key:0. for key in ('total', 'spectral_reconstruction', 'target_component_distortion')}; windows=0
    model.eval()
    with torch.no_grad():
        for start in range(0, len(ids), BATCH):
            b = batch(arrays, ids[start:start+BATCH], device)
            if not len(b['positive_features']): continue
            n = len(torch.unique(b['scene_positions']))
            terms = enhancement_terms(model.enhancer(b['positive_features']), b['coefficients'], b['scene_positions'])
            for key in values: values[key] += float(terms[key])*n
            windows += n
    return {key:val/windows for key,val in values.items()}


def verify_proposal():
    proposal=read(PREP/'protocol_proposal.json')
    for section in ('source_sha256','input_sha256','protected_sha256'):
        for path, expected in proposal[section].items(): assert sha(ROOT/path)==expected, path
    return proposal


def prepare(authorization):
    assert authorization and not OUT.exists()
    assert not (PREP/'protocol_proposal.json').exists() and not (PREP/'preflight_verification.json').exists()
    protected=prior.verify_proposal()['protected_sha256']
    arrays=load_arrays(); PREP.mkdir(exist_ok=True)
    # 复用两个观测缓存，不重新计算或改变声源配方。
    feature=np.empty((320,16,20,106),np.float32)
    for start in range(0,320,8):
        feature[start:start+8]=detection_observations(arrays['physical_features'][start:start+8],
            np.asarray(arrays['features'][start:start+8],np.float32))
    if (PREP/'detection_features.npy').exists():
        assert np.array_equal(feature,np.load(PREP/'detection_features.npy'))
    else:
        np.save(PREP/'detection_features.npy',feature)
    arrays['detection_features']=feature
    device=setup(); model=SplitMultisourceNetwork().to(device); initial=state_sha(model)
    positive_ids=np.flatnonzero(arrays['group_masks'][:,0].any(1) & np.isin(np.arange(320),arrays['train_indices']))
    warm_ids=np.r_[np.arange(8),positive_ids[:8]]
    b=batch(arrays,warm_ids,device)
    model.zero_grad(set_to_none=True); detection_terms(model,b)['total'].backward()
    assert all(p.grad is None for p in model.enhancer.parameters())
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.detector.parameters())
    model.zero_grad(set_to_none=True)
    masks=model.enhancer(b['positive_features']); assert torch.equal(masks,torch.ones_like(masks))
    enhanced=enhancement_terms(masks,b['coefficients'],b['scene_positions']); enhanced['total'].backward()
    assert all(p.grad is None for p in model.detector.parameters())
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.enhancer.parameters())
    c=b['coefficients'].cpu().numpy().astype(float); m=masks.detach().cpu().numpy().astype(float); pos=b['scene_positions'].cpu().numpy()
    q,r,s=(c[...,j] for j in range(3))
    expected=float(np.mean([np.mean(np.sum(q[pos==i]*m[pos==i]**2-2*r[pos==i]*m[pos==i]+s[pos==i]+s[pos==i]*(1-m[pos==i])**2,axis=(1,2))) for i in np.unique(pos)]))
    error=abs(expected-float(enhanced['total'])); assert error<2e-6
    cpu=SplitMultisourceNetwork().eval(); cpu.load_state_dict({k:v.detach().cpu() for k,v in model.state_dict().items()})
    with torch.no_grad():
        output=model.detector(b['detection_features'],b['directions'],b['candidate_valid'])
        out_cpu=cpu.detector(b['detection_features'].cpu(),b['directions'].cpu(),b['candidate_valid'].cpu())
        cpu_error=float((output.cpu()-out_cpu).abs().max()); assert cpu_error<2e-5
        permutation=torch.tensor(list(reversed(range(16))),device=device)
        permuted=model.detector(b['detection_features'][:,permutation],b['directions'][:,permutation],b['candidate_valid'][:,permutation])
        permutation_error=float((permuted-output[:,permutation]).abs().max()); assert permutation_error<2e-5
    assert state_sha(model)==initial
    save(PREP,'preflight_verification.json',dict(failed=0, gradients_disjoint=True, initial_enhancement_identity=True,
        enhancement_numpy_formula_max_error=error, CPU_GPU_detector_max_error=cpu_error,
        candidate_permutation_max_error=permutation_error, optimizer_steps=0, initial_state_sha256=initial))
    sources=[Path(__file__),ROOT/'python_tools/research/multisource_split_network.py',
        ROOT/'python_tools/research/candidate_joint_selection_objective_v2.py',ROOT/'python_tools/research/candidate_joint_validity_network.py',
        ROOT/'python_tools/research/candidate_neighborhood_tcn.py',ROOT/'python_tools/research/directional_tf_mask.py']
    inputs=[PREP/'detection_features.npy',PREP/'preflight_verification.json',prior.prior.PREP/'observed_features.npy',
        prior.PREP/'positive_reconstruction_coefficients.npy',prior.PREP/'anchor_index_map.npz',
        selection.DATA/'feature_cache.npz',selection.DATA/'window_records.json',selection.SUP/'selection_supervision_v2.npz',selection.SUP/'supervision_metadata.json']
    save(PREP,'protocol_proposal.json',dict(status='SINGLE_SPLIT_MODEL_READY_FOR_CURRENT_EXPLICIT_REQUEST',authorization=authorization,
        model_version=VERSION,model_budget=model_budget(model),seed=SEED,max_epochs=24,training_runs_cap=1,
        selection='Original four-group2/1/1/1 BCE+.25ranking; detector gradients separate from enhancement',
        checkpoint='Detector: highest development CAL F1 across24epochs at three frozen thresholds, then fewerFP, then earlier epoch; enhancer: first lowest CAL reconstruction+target distortion independently',
        enhancement='Independent24ch3block residual amplitude mask0.5..1.5, identity initial; positive-only reconstruction+1.0target-component distortion; same old coefficients/estimated beams',
        thresholds=list(THRESHOLDS),batch=BATCH,TRAIN_windows=256,CAL_windows=64,
        functional_goals=dict(precision=.8,recall=.8,matched_mae_deg=5,mask_SI_SDR_gain_vs_WDAS_db=0,target_power_transfer_vs_WDAS_db=-3),
        functional_goals_are_engineering_targets_not_user_supplied_thresholds=True,
        no_automatic_second_training=True,no_new_TEST=True,no_formal_product_changes=True,
        source_sha256={str(p.relative_to(ROOT)):sha(p) for p in sources},
        input_sha256={str(p.relative_to(ROOT)):sha(p) for p in inputs},protected_sha256=protected))
    print(dict(stage='PREPARED',failed=0,model_budget=model_budget(model)),flush=True)


def run_once(authorization):
    assert authorization and not OUT.exists()
    protocol=verify_proposal(); arrays=load_arrays(); device=setup(); model=SplitMultisourceNetwork().to(device)
    assert state_sha(model)==read(PREP/'preflight_verification.json')['initial_state_sha256']
    OUT.mkdir(); save(OUT,'protocol_lock.json',dict(protocol,authorization=authorization,proposal_sha256=sha(PREP/'protocol_proposal.json')))
    save(OUT,'training_started.json',dict(training_runs=1,seed=SEED,max_epochs=24,authorization=authorization,initial_state_sha256=state_sha(model)))
    det_optimizer=torch.optim.AdamW(model.detector.parameters(),lr=.001,weight_decay=.0001)
    enh_optimizer=torch.optim.AdamW(model.enhancer.parameters(),lr=.001,weight_decay=.0001)
    rng=np.random.default_rng(SEED); train=arrays['train_indices']; cal=arrays['cal_indices']
    best_key=None; best_det=None; best_enh=None; det_epoch=enh_epoch=0; enh_loss=float('inf'); history=[];det_steps=enh_steps=0
    tick=time.perf_counter()
    for epoch in range(1,EPOCHS+1):
        model.train()
        for ids in np.array_split(rng.permutation(train),16):
            b=batch(arrays,ids,device)
            det_optimizer.zero_grad(set_to_none=True); loss=detection_terms(model,b)['total'];assert torch.isfinite(loss)
            loss.backward(); torch.nn.utils.clip_grad_norm_(model.detector.parameters(),5);det_optimizer.step();det_steps+=1
            if len(b['positive_features']):
                enh_optimizer.zero_grad(set_to_none=True)
                loss=enhancement_terms(model.enhancer(b['positive_features']),b['coefficients'],b['scene_positions'])['total'];assert torch.isfinite(loss)
                loss.backward();torch.nn.utils.clip_grad_norm_(model.enhancer.parameters(),5);enh_optimizer.step();enh_steps+=1
        cal_logits=scores(model,arrays,cal,device);chosen,table,_=metrics(arrays,cal,cal_logits)
        key=(chosen['metrics']['f1'],-chosen['metrics']['fp'],-epoch)
        if best_key is None or key>best_key:
            best_key=key;det_epoch=epoch;best_det={k:v.detach().cpu().clone() for k,v in model.detector.state_dict().items()}
        enhanced=enhancement_evaluation(model,arrays,cal,device)
        if enhanced['total']<enh_loss:
            enh_loss=enhanced['total'];enh_epoch=epoch;best_enh={k:v.detach().cpu().clone() for k,v in model.enhancer.state_dict().items()}
        row=dict(epoch=epoch,CAL_detection=chosen,CAL_enhancement=enhanced,detector_steps=det_steps,enhancer_steps=enh_steps,
            selected_detector_epoch=det_epoch,selected_enhancer_epoch=enh_epoch,elapsed_seconds=time.perf_counter()-tick)
        history.append(row);print(row,flush=True)
    model.detector.load_state_dict(best_det);model.enhancer.load_state_dict(best_enh);model.eval()
    logits=scores(model,arrays,np.arange(320),device);probabilities=1/(1+np.exp(-logits.astype(float)))
    chosen,table,predictions=metrics(arrays,cal,logits[cal]);train_chosen,_,_=metrics(arrays,train,logits[train])
    cal_masks=np.empty((64,16,67,64),np.float32)
    with torch.no_grad():
        for offset in range(0,64,4):
            ids=cal[offset:offset+4]
            features=torch.from_numpy(np.array(arrays['features'][ids],dtype=np.float32,copy=True)).to(device)
            cal_masks[offset:offset+4]=model.enhancer(features).cpu().numpy()
    torch.save(dict(state_dict={k:v.detach().cpu() for k,v in model.state_dict().items()},model_version=VERSION,
        detector_epoch=det_epoch,enhancer_epoch=enh_epoch,epoch=det_epoch,protocol_sha256=sha(OUT/'protocol_lock.json')),OUT/'best_checkpoint.pt')
    np.savez_compressed(OUT/'candidate_predictions.npz',logits=logits,probabilities=probabilities)
    np.savez_compressed(OUT/'CAL_mask_predictions.npz',masks=cal_masks,scene_indices=cal)
    save(OUT,'training_history.json',dict(epochs=history,detector_steps=det_steps,enhancer_steps=enh_steps,
        selected_detector_epoch=det_epoch,selected_enhancer_epoch=enh_epoch,elapsed_training_and_export_seconds=time.perf_counter()-tick))
    save(OUT,'CAL_all_threshold_predictions.json',predictions)
    save(OUT,'summary.json',dict(status='ONE_SPLIT_MODEL_TRAINED_PENDING_FULL_FUNCTION_EVALUATION',training_runs=1,
        selected_epoch=det_epoch,selected_enhancer_epoch=enh_epoch,selected_threshold=chosen['threshold'],
        detection=chosen['metrics'],TRAIN_diagnostic=train_chosen,threshold_table=table,
        model_budget=model_budget(model),functional_acceptance=False,new_TEST_runs=0,formal_builds=0,board_measurements=0))
    verify_proposal();print(dict(stage='TRAINING_COMPLETE',detection=chosen['metrics']),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--prepare',action='store_true');parser.add_argument('--run-once',action='store_true')
    parser.add_argument('--authorization',required=True);args=parser.parse_args()
    assert args.prepare != args.run_once
    (prepare if args.prepare else run_once)(args.authorization)
