"""Inactive one-run fine-tuner; requires a new explicit finite budget."""
import argparse
import time
import numpy as np
import torch
import train_multisource_split_one_20261009 as prior
import prepare_multisource_waveform_tuning_20261009 as preparation
from research import multisource_waveform_quality_tuning as tuning

ROOT,PREP = prior.ROOT,preparation.OUT
OUT = ROOT/'results/multisource_waveform_tuning_one_run_20261009'
SEED,EPOCHS,BATCH,THRESHOLDS = prior.SEED,24,16,prior.THRESHOLDS
read,sha,save,state_sha,setup = prior.read,prior.sha,prior.save,prior.state_sha,prior.setup
VERSION = tuning.VERSION


def load_arrays():
    arrays = prior.load_arrays()
    for key in ('mixture_spectra','target_spectra','reference','guards'):
        arrays[key] = np.load(PREP/(key+'.npy'),mmap_mode='r')
    return arrays


def batch(arrays,ids,device,include_positive=True):
    ids = np.asarray(ids)
    b = prior.batch(arrays,ids,device,False)
    if include_positive:
        positions,candidates = np.nonzero(arrays['group_masks'][ids,0])
        anchors = arrays['anchor_index_map'][ids[positions],candidates]
        b.update(positive_features=torch.from_numpy(np.array(arrays['features'][ids[positions],candidates],np.float32)).to(device),
            positions=torch.from_numpy(positions).to(device))
        for key in ('mixture_spectra','target_spectra','reference','guards'):
            b[key] = torch.from_numpy(np.array(arrays[key][anchors],copy=True)).to(device)
    return b


def waveform_terms(model,b):
    return tuning.waveform_terms(model.enhancer(b['positive_features']),b['mixture_spectra'],b['target_spectra'],b['reference'],b['guards'],b['positions'])


def enhance_evaluation(model,arrays,ids,device):
    totals = {k:0. for k in ('total','quality','target_distortion','mean_SI_SDR_gain_db','mean_target_power_transfer_db')}
    windows = 0;model.eval()
    with torch.no_grad():
        for offset in range(0,len(ids),BATCH):
            b = batch(arrays,ids[offset:offset+BATCH],device)
            if not len(b['positions']):
                continue
            n = len(torch.unique(b['positions'])); windows += n
            terms = waveform_terms(model,b)
            for k in totals:
                totals[k] += float(terms[k])*n
    return {k:v/windows for k,v in totals.items()}


def detector_key(row,epoch):
    m = row['metrics']; qualified = m['precision'] >= .8
    return (qualified,m['recall'] if qualified else m['f1'],m['f1'],-m['fp'],row['threshold'],-epoch)


def metrics(arrays,ids,logits):
    _,table,predictions = prior.metrics(arrays,ids,logits)
    chosen = max(table,key=lambda row:detector_key(row,0))
    return chosen,table,predictions


def verify_proposal():
    protocol = read(PREP/'extraction_protocol.json')
    for section in ('source_sha256','input_sha256','protected_sha256'):
        for p,h in protocol[section].items():
            assert sha(ROOT/p) == h,p
    assert read(PREP/'preflight_verification.json')['failed'] == 0
    return protocol


def run_once(authorization):
    assert authorization and not OUT.exists(), 'New explicit budget and unused one-run directory required'
    protocol = verify_proposal();arrays = load_arrays();device = setup()
    model = preparation.initialized_model(device)
    assert state_sha(model) == read(PREP/'preflight_verification.json')['initial_state_sha256']
    OUT.mkdir();save(OUT,'protocol_lock.json',dict(protocol,authorization=authorization))
    save(OUT,'training_started.json',dict(training_runs=1,max_epochs=24,seed=SEED,authorization=authorization,initial_state_sha256=state_sha(model)))
    train,cal = arrays['train_indices'],arrays['cal_indices']
    det_optimizer = torch.optim.AdamW(model.detector.parameters(),lr=.0003,weight_decay=.0001)
    enh_optimizer = torch.optim.AdamW(model.enhancer.parameters(),lr=.001,weight_decay=.0001)
    def snapshot(module):
        return {k:v.detach().cpu().clone() for k,v in module.state_dict().items()}
    initial_logits = prior.scores(model,arrays,cal,device)
    chosen,_,_ = metrics(arrays,cal,initial_logits)
    best_det,best_enh = snapshot(model.detector),snapshot(model.enhancer)
    best_key = detector_key(chosen,0);det_epoch = enh_epoch = 0
    enhanced = enhance_evaluation(model,arrays,cal,device)
    assert abs(enhanced['mean_SI_SDR_gain_db']) < 1e-6 and enhanced['target_distortion'] == 0
    best_enh_key = (0.,0.,0)
    save(OUT,'initial_checkpoint_evaluation.json',dict(epoch=0,detection=chosen,enhancement=enhanced))
    history=[];det_steps=enh_steps=0;rng=np.random.default_rng(SEED);tick=time.perf_counter()
    for epoch in range(1,25):
        model.train()
        for ids in np.array_split(rng.permutation(train),16):
            b = batch(arrays,ids,device)
            det_optimizer.zero_grad(set_to_none=True)
            loss = tuning.detection_terms(model,b)['total'];assert torch.isfinite(loss)
            loss.backward();torch.nn.utils.clip_grad_norm_(model.detector.parameters(),5);det_optimizer.step();det_steps += 1
            if len(b['positions']):
                enh_optimizer.zero_grad(set_to_none=True)
                loss = waveform_terms(model,b)['total'];assert torch.isfinite(loss)
                loss.backward();torch.nn.utils.clip_grad_norm_(model.enhancer.parameters(),5);enh_optimizer.step();enh_steps += 1
        chosen,table,_ = metrics(arrays,cal,prior.scores(model,arrays,cal,device))
        key = detector_key(chosen,epoch)
        if key > best_key:
            best_key=key;best_det=snapshot(model.detector);det_epoch=epoch
        enhanced=enhance_evaluation(model,arrays,cal,device)
        if enhanced['mean_SI_SDR_gain_db'] >= 0 and enhanced['mean_target_power_transfer_db'] >= -3:
            key=(enhanced['mean_SI_SDR_gain_db'],-enhanced['target_distortion'],-epoch)
            if key > best_enh_key:
                best_enh_key=key;best_enh=snapshot(model.enhancer);enh_epoch=epoch
        row=dict(epoch=epoch,CAL_detection=chosen,CAL_enhancement=enhanced,detector_steps=det_steps,enhancer_steps=enh_steps,
            selected_detector_epoch=det_epoch,selected_enhancer_epoch=enh_epoch,elapsed_seconds=time.perf_counter()-tick)
        history.append(row);print(row,flush=True)
    model.detector.load_state_dict(best_det);model.enhancer.load_state_dict(best_enh);model.eval()
    logits=prior.scores(model,arrays,np.arange(320),device)
    chosen,table,predictions=metrics(arrays,cal,logits[cal])
    masks=np.empty((64,16,67,64),np.float32)
    with torch.no_grad():
        for offset in range(0,64,4):
            features=torch.from_numpy(np.array(arrays['features'][cal[offset:offset+4]],np.float32)).to(device)
            masks[offset:offset+4]=model.enhancer(features).cpu().numpy()
    torch.save(dict(state_dict={k:v.detach().cpu() for k,v in model.state_dict().items()},model_version=VERSION,
        detector_epoch=det_epoch,enhancer_epoch=enh_epoch,epoch=det_epoch,protocol_sha256=sha(OUT/'protocol_lock.json')),OUT/'best_checkpoint.pt')
    np.savez_compressed(OUT/'candidate_predictions.npz',logits=logits,probabilities=1/(1+np.exp(-logits.astype(float))))
    np.savez_compressed(OUT/'CAL_mask_predictions.npz',masks=masks,scene_indices=cal)
    save(OUT,'CAL_all_threshold_predictions.json',predictions)
    save(OUT,'training_history.json',dict(epochs=history,detector_steps=det_steps,enhancer_steps=enh_steps,
        selected_detector_epoch=det_epoch,selected_enhancer_epoch=enh_epoch,elapsed_training_and_export_seconds=time.perf_counter()-tick))
    save(OUT,'summary.json',dict(status='WAVEFORM_FINE_TUNING_DONE_PENDING_FULL_CHAIN_VALIDATION',training_runs=1,
        selected_epoch=det_epoch,selected_enhancer_epoch=enh_epoch,selected_threshold=chosen['threshold'],detection=chosen['metrics'],
        selected_enhancement_anchor_evaluation=enhance_evaluation(model,arrays,cal,device),threshold_table=table,
        model_budget=tuning.model_budget(model),functional_acceptance=False,PC_frozen=False,FPGA_frozen=False,
        new_TEST_runs=0,formal_builds=0,board_measurements=0))
    verify_proposal()
    print(dict(stage='ONE_FINE_TUNING_COMPLETE',detection=chosen['metrics'],detector_epoch=det_epoch,enhancer_epoch=enh_epoch),flush=True)


if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--run-once',action='store_true');parser.add_argument('--authorization',required=True)
    args=parser.parse_args()
    if args.run_once:
        run_once(args.authorization)
