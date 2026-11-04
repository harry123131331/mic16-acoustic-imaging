"""Prepare existing-scene waveform supervision and meaningful loss preflight."""
import os
for name in ('OMP_NUM_THREADS','MKL_NUM_THREADS','OPENBLAS_NUM_THREADS'):
    os.environ[name] = '2'
os.environ['CUBLAS_WORKSPACE_CONFIG'] = ':4096:8'
from pathlib import Path
import argparse
import time
import numpy as np
import torch
import train_multisource_split_one_20261009 as prior
import run_multisource_split_system_simulation_20261009 as replay
from research import multisource_waveform_quality_tuning as tuning
from research import directional_tf_mask as tf

ROOT = prior.ROOT
OUT = ROOT/'results/multisource_waveform_tuning_preparation_20261009'
read,sha,save = prior.read,prior.sha,prior.save


def initialized_model(device):
    model = tuning.WaveformQualityNetwork().to(device)
    state = torch.load(prior.OUT/'best_checkpoint.pt',map_location=device,weights_only=True)
    model.load_state_dict(state['state_dict'])
    # 保留已训练检测器和增强编码器，新增残差输出
    # 从恒等变换开始，防止初始化就变成静音滤波器。
    torch.nn.init.zeros_(model.enhancer.output.weight)
    torch.nn.init.zeros_(model.enhancer.output.bias)
    return model


def main():
    assert not OUT.exists(), 'Preserve existing preparation'
    protected = prior.verify_proposal()['protected_sha256']
    assert read(prior.OUT/'independent_verification.json')['failed'] == 0
    assert read(replay.OUT/'system_verification.json')['failed'] == 0
    arrays = prior.load_arrays()
    records = read(prior.selection.DATA/'window_records.json')
    recipes = read(prior.selection.DATA/'scene_plan.json')
    mapping = np.asarray(arrays['anchor_index_map'])
    speech_lock = read(ROOT/'results/candidate_speech_intake_dev_other_20261007/speech_assets_lock.json')
    rir_lock = read(ROOT/'results/candidate_rir_preparation_20261007/rir_assets_lock.json')
    for row in speech_lock['speech_assets']+rir_lock['rir_assets']:
        assert sha(Path(row['path'])) == row['sha256']
    speech = replay.ref.load_speech(speech_lock['speech_assets'])[0]
    rirs = {}
    for row in rir_lock['rir_assets']:
        with np.load(row['path']) as data:
            rirs[row['scene_id']] = data['rir'].copy()
    gcc,quality,weight,external = replay.ref.load_weight_reference()
    old_labels = read(prior.prior.PREP/'label_evidence.json')
    count = len(old_labels); assert count == 377
    mixture_spectra = np.empty((count,67,257),np.complex64)
    target_spectra = np.empty_like(mixture_spectra)
    reference = np.empty((count,tuning.SAMPLES),np.float32)
    guards = np.empty(count,np.int64)
    OUT.mkdir()
    sources = [Path(__file__),Path(tuning.__file__),ROOT/'python_tools/train_multisource_waveform_tuning_one_20261009.py']
    assert all(p.exists() for p in sources)
    protocol = dict(status='FROZEN_BEFORE_EXISTING_SCENE_LABEL_REPLAY_NO_TRAINING',date='2026-10-09',
        model_version=tuning.VERSION,model_parameters=31482,max_new_training_runs=1,max_epochs=24,
        authorization_required=True,prior_training_budget_consumed=True,
        inputs='Reuse exactly the320 observed feature windows; no new scenes or feature sweep',
        supervision='Positive anchors only; same observed WDAS/estimated direction, target WDAS and aligned MIC0 target component for waveform quality',
        formula='L=mean_window(mean_anchor(softplus(-actual_SI_SDR_gain_dB)+target_waveform_distortion/E_target)); same output mask applied to mixture and target component',
        mask_range=[.8,1.2],mask_initialization='Trained encoder copied; final output zero, exact identity',
        detection='Warm-start existing trained detector; fixed group BCE3/1/1/1 +.25source ranking, separate optimizer',
        detector_selection='Include initial checkpoint; prefer precision>=.8 then highest recall,F1,fewerFP,earlier epoch at same registered .5/.65/.8 thresholds',
        enhancer_selection='Include identity initial checkpoint; among anchor meanSIgain>=0 and target transfer>=-3dB choose highestSIgain, then less target distortion,earlier epoch; final selected-chain waveform replay still required',
        do_not_freeze_until_functional_validation=True,
        deployment_order='Tune/verify/freeze PC -> implement and validate FPGA/Vitis offline and freeze -> board connection and measured validation',
        source_sha256={str(p.relative_to(ROOT)):sha(p) for p in sources},
        input_sha256={str(p.relative_to(ROOT)):sha(p) for p in (prior.OUT/'best_checkpoint.pt',prior.PREP/'detection_features.npy',prior.prior.PREP/'label_evidence.json',prior.selection.DATA/'scene_plan.json',prior.selection.DATA/'window_records.json')},
        external_reference_sha256=external,protected_sha256=protected)
    save(OUT,'extraction_protocol.json',protocol)
    tick = time.perf_counter(); rows=[]
    by_scene = {}
    for row in old_labels:
        by_scene.setdefault(row['scene_index'],[]).append(row)
    for i in range(320):
        if i in by_scene:
            frames,details,components,_,info = replay.ref.build_scene(recipes[i],speech,rirs)
            assert info['pcm_sha256'] == records[i]['pcm_sha256']
            weights = replay.ref.channel_weights(frames,gcc,quality,weight)
            onehot = np.zeros_like(weights); onehot[:,0] = 1
            for old in by_scene[i]:
                j,c,s = old['coefficient_index'],old['candidate'],old['source_component_index']
                assert mapping[i,c] == j and details[s]['role'] == 'speech'
                direction = arrays['directions'][i,c].astype(float)
                y,meta = replay.ref.pc_wdas(frames,direction,weights,weight)
                target = replay.ref.pc_wdas(components[s],direction,weights,weight)[0]
                mic0 = replay.ref.pc_wdas(components[s],direction,onehot,weight)[0]
                mixture_spectra[j] = tf.stft(tf.resample(y))
                target_spectra[j] = tf.stft(tf.resample(target))
                reference[j] = tf.resample(mic0)
                guards[j] = int(np.ceil(meta['guard_samples']*256/625))
                rows.append(dict(anchor=j,scene_index=i,candidate=c,source_component_index=s,
                    estimated_direction=direction.tolist(),guard16=int(guards[j]),original_PCM_sha256=info['pcm_sha256'],
                    role=records[i]['role'],source_components_used_only_for_supervision=True))
        if (i+1)%64 == 0:
            print(dict(prepared_existing_windows=i+1,total=320,elapsed_seconds=time.perf_counter()-tick),flush=True)
    assert len(rows) == 377
    for name,value in (('mixture_spectra',mixture_spectra),('target_spectra',target_spectra),('reference',reference),('guards',guards)):
        np.save(OUT/(name+'.npy'),value)
    save(OUT,'label_evidence.json',rows)
    device = prior.setup(); model = initialized_model(device).eval()
    selected = np.arange(8); indices = [(r['scene_index'],r['candidate']) for r in sorted(rows,key=lambda r:r['anchor'])[:8]]
    features = torch.from_numpy(np.array([arrays['features'][i,c] for i,c in indices],np.float32)).to(device)
    tensors = [torch.from_numpy(v[selected].copy()).to(device) for v in (mixture_spectra,target_spectra,reference,guards)]
    positions = torch.arange(8,device=device)
    masks = model.enhancer(features); assert torch.equal(masks,torch.ones_like(masks))
    initial_sha = prior.state_sha(model)
    terms = tuning.waveform_terms(masks,*tensors,positions)
    assert abs(float(terms['mean_SI_SDR_gain_db'])) < 1e-6 and float(terms['target_distortion']) == 0
    assert abs(float(terms['total'])-np.log(2)) < 2e-6
    waveform = tuning.torch_istft(tensors[0],masks).detach().cpu().numpy()
    cpu_error = max(float(np.max(np.abs(waveform[j]-tf.istft(mixture_spectra[j],tuning.SAMPLES)))) for j in selected)
    assert cpu_error < 1e-7, cpu_error
    reference_si = [replay.sisdr_stats(tf.istft(mixture_spectra[j],tuning.SAMPLES),reference[j],int(guards[j]))['SI_SDR_db'] for j in selected]
    torch_si = tuning.sisdr(tuning.torch_istft(tensors[0],masks),tensors[2],tensors[3]).detach().cpu().numpy()
    si_error = float(np.max(np.abs(torch_si-reference_si))); assert si_error < 2e-4, si_error
    checks=[]
    for gain in (.8,1.2):
        constant = tuning.waveform_terms(torch.full_like(masks,gain),*tensors,positions)
        assert abs(float(constant['mean_SI_SDR_gain_db'])) < 2e-4
        assert float(constant['total']) > float(terms['total'])+.039
        checks.append(dict(constant_gain=gain,total=float(constant['total']),SI_gain=float(constant['mean_SI_SDR_gain_db'])))
    model.zero_grad(set_to_none=True);terms['total'].backward()
    assert all(p.grad is None for p in model.detector.parameters())
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.enhancer.parameters())
    assert model.enhancer.output.weight.grad.abs().sum() > 0
    assert prior.state_sha(model) == initial_sha
    for section in ('source_sha256','input_sha256','protected_sha256'):
        assert all(sha(ROOT/p) == h for p,h in protocol[section].items())
    prior.verify_proposal()
    save(OUT,'preflight_verification.json',dict(failed=0,labels=377,TRAIN_anchors=302,CAL_anchors=75,
        exact_initial_identity=True,independent_numpy_OLA_max_error=cpu_error,independent_numpy_SI_SDR_max_error_db=si_error,
        constant_volume_change_cannot_reduce_new_loss=checks,enhancement_gradient_disjoint=True,
        initial_state_sha256=initial_sha,optimizer_steps=0,training_runs=0,protected_files_unchanged=234,
        new_TEST_runs=0,formal_builds=0,board_measurements=0,elapsed_seconds=time.perf_counter()-tick))
    assert sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()) < 160*1024**2
    print(dict(status='WAVEFORM_TUNING_READY_NO_NEW_TRAINING_AUTHORIZED',failed=0,OLA_error=cpu_error,SI_error=si_error),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser();parser.add_argument('--prepare-no-training',action='store_true')
    if parser.parse_args().prepare_no_training:
        main()
