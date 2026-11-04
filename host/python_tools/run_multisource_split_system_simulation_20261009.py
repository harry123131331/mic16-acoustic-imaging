"""Replay the approved direction-conditioned validity/mask chain on frozen CAL.



Inference uses observed PCM only. Truth components enter subsequent evaluation.

All candidate WDAS beams are paid for in runtime, even candidates not selected.

"""

import os

for name in ('OMP_NUM_THREADS', 'MKL_NUM_THREADS', 'OPENBLAS_NUM_THREADS'):

    os.environ[name] = '2'

from pathlib import Path

import sys

import time

import numpy as np

import torch

import soundfile as sf

from scipy.signal import lfilter

import train_multisource_split_one_20261009 as run

from research import multisource_split_network as split

import run_neural_multisource_enhancement_simulation_20261008 as ref

from research import directional_tf_mask as tf

from research.candidate_rescue_synthesis import BACKGROUND_RMS, rms

sys.path.insert(0, str(run.ROOT / 'tools/verification'))

from verify_joint_selection_v2_run_20261008 import independent_score, aggregate, angles, compare



ROOT = run.ROOT

OUT = ROOT / 'results/neural_multisource_enhancement_split_20261009'

DEMOS = [264, 270, 274, 278, 282, 258]

read, sha, save = run.read, run.sha, run.save





def power(x, guard):

    return float(np.sum(np.asarray(x, float)[guard:]**2))





def ratio(a, b):

    return float(10*np.log10(max(a, 1e-30)/max(b, 1e-30)))





def sisdr_stats(estimate, reference, guard):

    x, s = np.asarray(estimate, float)[guard:].copy(), np.asarray(reference, float)[guard:].copy()

    x -= x.mean(); s -= s.mean()

    a, b, dot = float(x@x), float(s@s), float(x@s)

    projection = dot*dot/max(b, 1e-30)

    return dict(estimate_centered_power=a, reference_centered_power=b, centered_dot=dot,

                SI_SDR_db=ratio(projection, max(a-projection, 1e-30)))





def statistics(values):

    a = np.asarray(values, float)

    return dict(count=len(a), mean=float(a.mean()) if len(a) else None,

                median=float(np.median(a)) if len(a) else None,

                minimum=float(a.min()) if len(a) else None, maximum=float(a.max()) if len(a) else None,

                positive=int((a > 0).sum()), negative=int((a < 0).sum()))





def main():

    assert not OUT.exists(), 'Preserve completed/partial evidence; never rerun training'

    proposal = run.verify_proposal(); run.prior.preparation.verify_prior()

    assert read(run.OUT / 'independent_verification.json')['failed'] == 0

    summary = read(run.OUT / 'summary.json'); threshold = summary['selected_threshold']

    predictions = {r['index']: r for r in read(run.OUT / 'CAL_all_threshold_predictions.json')[str(threshold)]}

    arrays = run.load_arrays()

    records = read(run.selection.DATA / 'window_records.json')

    recipes = read(run.selection.DATA / 'scene_plan.json')

    with np.load(run.selection.DATA / 'feature_cache.npz') as data:

        references = data['quiet_references'].copy()

    with np.load(run.OUT / 'candidate_predictions.npz') as data:

        expected_logits = data['logits'].copy()

    with np.load(run.OUT / 'CAL_mask_predictions.npz') as data:

        expected_masks = data['masks'].copy()

    speech_lock = read(ROOT / 'results/candidate_speech_intake_dev_other_20261007/speech_assets_lock.json')

    rir_lock = read(ROOT / 'results/candidate_rir_preparation_20261007/rir_assets_lock.json')

    for row in speech_lock['speech_assets']:

        assert sha(Path(row['path'])) == row['sha256']

    speech, _ = ref.load_speech(speech_lock['speech_assets'])

    rirs = {}

    for row in rir_lock['rir_assets']:

        assert sha(Path(row['path'])) == row['sha256']

        with np.load(row['path']) as data:

            rirs[row['scene_id']] = data['rir'].copy()

    gcc, quality, weight, external = ref.load_weight_reference()

    device = run.setup('cpu')

    model = split.SplitMultisourceNetwork().eval()

    checkpoint = torch.load(run.OUT / 'best_checkpoint.pt', map_location='cpu', weights_only=True)

    model.load_state_dict(checkpoint['state_dict'])

    state_before = run.state_sha(model)

    with torch.no_grad():

        model.detector(torch.zeros(1,16,20,106),torch.zeros(1,16,2),torch.ones(1,16,dtype=torch.bool))

    projector = ref.TfZoneProjector()

    aa, ee = np.meshgrid(projector.grid.azimuths_deg.astype(float), projector.grid.elevations_deg.astype(float))

    az, el = np.deg2rad(aa.ravel()), np.deg2rad(ee.ravel())

    cosine = np.sin(el[:, None])*np.sin(el[None, :])+np.cos(el[:, None])*np.cos(el[None, :])*np.cos(az[:, None]-az[None, :])

    allowed = np.rad2deg(np.arccos(np.clip(cosine, -1, 1))) >= 15

    OUT.mkdir(); (OUT / 'audio').mkdir()

    save(OUT, 'simulation_protocol.json', dict(status='FROZEN_BEFORE_APPROVED_FULL_CHAIN_REPLAY',

        scene_indices=arrays['cal_indices'].tolist(), fixed_illustration_indices=DEMOS,

        checkpoint_sha256=sha(run.OUT / 'best_checkpoint.pt'), threshold=threshold,

        inference='Observed physical proposals/88features -> all candidate WDAS/TF observations -> independent detector with neighborhood comparisons ->10deg NMS max8 -> independent selected-direction residual enhancer/OLA',

        feature_precision='Same FP16 cache rounding followed by FP32 CPU inference; not an FPGA quantization proof',

        enhancement_baseline='Same16kHz estimated-direction WDAS without neural mask, plus same16kHz MIC0 input',

        evaluation='Truth components only after inference; same mixture-derived weights and estimated directions; same predicted mask applies linearly to each component',

        SI_SDR_reference='Known MIC0 common-delay/Lagrange FIR component at same estimated direction, resampled16kHz; no fitted time shift',

        timing='CPU2threads includes physical proposals, weights, ALL candidate beams and TF features, neural/decode, selected OLA; excludes scene reconstruction, evaluation and IO; observation wait1.048576s separate',

        external_reference_sha256=external, protected_sha256=proposal['protected_sha256'],

        no_new_training=True, no_new_TEST=True, no_formal_build=True, board_measurements=0,

        output_limit_bytes=48*1024**2, generator_sha256=sha(Path(__file__))))

    scenes, matched, outputs, energy_rows, demo_data = [], [], [], [], {}

    started = time.perf_counter()

    for ordinal, index_value in enumerate(arrays['cal_indices']):

        index = int(index_value); record = records[index]

        frames, details, components, _, info = ref.build_scene(recipes[index], speech, rirs)

        assert info['pcm_sha256'] == record['pcm_sha256'] and details == record['source_details']

        begin = time.perf_counter(); quiet = references[record['reference_index']]

        maps, _ = ref.extract_neural_features(projector, frames, quiet)

        votes = ref.front.votes_from_maps(projector, maps, frames, quiet, allowed)

        pool = ref.fixed_bounded_pool(ref.clusters(votes, np.ones(480, bool), 10.))

        rebuilt = np.asarray([r['direction'] for r in pool], float)

        c = len(pool); valid = arrays['candidate_valid'][index]

        assert c == record['candidate_count'] == int(valid.sum())

        error_direction = float(np.max(np.abs(rebuilt-arrays['directions'][index, :c])))

        assert error_direction < 5e-6, (index, error_direction)

        # 与准备阶段一致，回放导向使用原已存候选精度。

        directions = arrays['directions'][index, :c].astype(float)

        physical, _ = ref.extract_candidate_features(frames, quiet, rebuilt, votes)

        physical_padded = np.zeros((16,20,88),np.float32); physical_padded[:c] = physical

        physical_error = float(np.max(np.abs(physical_padded-arrays['physical_features'][index])))

        assert physical_error < 2e-5, (index, physical_error)

        after_proposals = time.perf_counter()

        weights = ref.channel_weights(frames, gcc, quality, weight)

        beams_and_meta = [ref.pc_wdas(frames, direction, weights, weight) for direction in directions]

        beams, metadata = zip(*beams_and_meta)

        features, spectral_weight, spectra = tf.observed_features(frames, directions, quiet, beams)

        observed = np.zeros((1, 16, 67, 64, 10), np.float32)

        observed[0, :c] = features.astype(np.float16).astype(np.float32)

        detection_input = split.detection_observations(physical_padded, observed[0])

        detection_error = float(np.max(np.abs(detection_input-arrays['detection_features'][index])))

        assert detection_error < 2e-5

        after_observations = time.perf_counter()

        with torch.no_grad():

            logits = model.detector(torch.from_numpy(detection_input[None]),

                torch.from_numpy(arrays['directions'][index:index+1]),torch.from_numpy(valid[None])).numpy()[0]

        probabilities = 1/(1+np.exp(-logits.astype(float)))

        actual, selected = ref.decode_joint_validity(arrays['directions'][index], probabilities, valid, threshold)

        masks = np.zeros((16,67,64),np.float32)

        if selected:

            with torch.no_grad():

                masks[selected] = model.enhancer(torch.from_numpy(observed[0,selected])).numpy()

        after_neural = time.perf_counter()

        n16 = len(tf.resample(beams[0]))

        unmasked = [tf.istft(spectra[j], n16) for j in selected]

        enhanced = [split.apply_mask(spectra[j], masks[j], n16) for j in selected]

        after_mask = time.perf_counter()

        feature_error = float(np.max(np.abs(observed[0]-np.asarray(arrays['features'][index], np.float32))))

        logit_error = float(np.max(np.abs(logits-expected_logits[index])))

        mask_error = float(np.max(np.abs(masks[selected]-expected_masks[ordinal,selected]))) if selected else 0.

        assert feature_error == 0 and logit_error < 5e-5 and mask_error < 2e-5

        assert selected == predictions[index]['selected_candidates'] and actual == predictions[index]['outputs']

        # 监督信息和真实分量仅能在此边界之后进入。

        truth, gray, clear = run.selection.previous.context(record)

        metrics = independent_score(actual, truth, gray, clear)

        assert tuple(metrics[k] for k in ('tp', 'fp', 'fn')) == tuple(predictions[index]['metrics'][k] for k in ('tp', 'fp', 'fn'))

        rng = np.random.default_rng(recipes[index]['background_seed'])

        noise = lfilter([1.], [1., -.8], rng.standard_normal((ref.N, 16)), axis=0)

        background = (noise*(BACKGROUND_RMS/rms(noise))).astype(np.float32)

        assert ref.array_sha(background) == info['background_sha256']

        raw = tf.resample(frames.reshape(ref.N, 16)[:, 0])

        sources = [j for j, d in enumerate(details) if d['role'] == 'speech' and d['active_samples'] >= 7813]

        pairs = {r['output']: r for r in metrics['pairs']}

        scene_matches = []

        closure_error = 0.

        for output_id, candidate in enumerate(selected):

            direction = actual[output_id]; mask = masks[candidate]

            guard = int(np.ceil(metadata[candidate]['guard_samples']*256/625))

            component_beams = [tf.resample(ref.pc_wdas(v, direction, weights, weight)[0]) for v in components]

            masked_components = [split.apply_mask(tf.stft(v), mask, n16) for v in component_beams]

            bg = tf.resample(ref.pc_wdas(background, direction, weights, weight)[0])

            masked_bg = split.apply_mask(tf.stft(bg), mask, n16)

            closure = np.max(np.abs(np.sum(masked_components, axis=0)+masked_bg-enhanced[output_id]))

            closure_error = max(closure_error, float(closure)); assert closure < 1e-7

            component_powers = [dict(role=d['role'], input=power(tf.resample(v[:, 0]), guard),

                WDAS=power(b, guard), masked=power(m, guard)) for d, v, b, m in zip(details, components, component_beams, masked_components)]

            bg_powers = dict(input=power(tf.resample(background[:, 0]), guard), WDAS=power(bg, guard), masked=power(masked_bg, guard))

            out = dict(scene_index=index, output_index=output_id, candidate=candidate, direction=direction,

                validity_probability=float(probabilities[candidate]), matched=output_id in pairs, guard16=guard,

                mask_min=float(mask.min()), mask_max=float(mask.max()), mask_mean=float(mask.mean()),

                component_powers=component_powers, background_powers=bg_powers,

                background_attenuation_vs_MIC0_db=ratio(bg_powers['input'], bg_powers['masked']),

                background_mask_attenuation_vs_WDAS_db=ratio(bg_powers['WDAS'], bg_powers['masked']))

            outputs.append(out)

            if output_id not in pairs:

                continue

            pair = pairs[output_id]; source = sources[pair['truth']]

            target_raw = tf.resample(components[source][:, 0])

            target_beam, target_masked = component_beams[source], masked_components[source]

            values = [target_raw, raw-target_raw, target_beam, unmasked[output_id]-target_beam,

                      target_masked, enhanced[output_id]-target_masked]

            energy = np.array([power(v, guard) for v in values])

            onehot = np.zeros_like(weights); onehot[:, 0] = 1

            target_reference = tf.resample(ref.pc_wdas(components[source], direction, onehot, weight)[0])

            aligned_input = tf.resample(ref.pc_wdas(frames, direction, onehot, weight)[0])

            si = {name: sisdr_stats(v, target_reference, guard) for name, v in

                  (('input', aligned_input), ('WDAS', unmasked[output_id]), ('masked', enhanced[output_id]),

                   ('target_WDAS', target_beam), ('target_masked', target_masked))}

            other_speech = [p for j, p in enumerate(component_powers) if j != source and p['role'] == 'speech']

            other_input = sum(p['input'] for p in other_speech)

            other_WDAS = sum(p['WDAS'] for p in other_speech)

            other_masked = sum(p['masked'] for p in other_speech)

            item = dict(scene_index=index, output_index=output_id, truth_index=pair['truth'], source_index=source,

                direction=direction, angular_error_deg=pair['error_deg'], guard16=guard, energy_row=len(energy_rows),

                input_component_SIR_db=ratio(energy[0], energy[1]), WDAS_component_SIR_db=ratio(energy[2], energy[3]),

                masked_component_SIR_db=ratio(energy[4], energy[5]),

                SIR_gain_vs_input_db=ratio(energy[4], energy[5])-ratio(energy[0], energy[1]),

                SIR_mask_gain_vs_WDAS_db=ratio(energy[4], energy[5])-ratio(energy[2], energy[3]),

                target_power_transfer_vs_input_db=ratio(energy[4], energy[0]),

                target_mask_power_transfer_vs_WDAS_db=ratio(energy[4], energy[2]),

                SI_SDR=si, SI_SDR_gain_vs_input_db=si['masked']['SI_SDR_db']-si['input']['SI_SDR_db'],

                SI_SDR_mask_gain_vs_WDAS_db=si['masked']['SI_SDR_db']-si['WDAS']['SI_SDR_db'],

                background_attenuation_vs_MIC0_db=out['background_attenuation_vs_MIC0_db'],

                background_mask_attenuation_vs_WDAS_db=out['background_mask_attenuation_vs_WDAS_db'],

                other_speech_attenuation_vs_MIC0_db=ratio(other_input, other_masked) if other_speech else None,

                other_speech_mask_attenuation_vs_WDAS_db=ratio(other_WDAS, other_masked) if other_speech else None)

            energy_rows.append(energy); matched.append(item); scene_matches.append(item)

        audio = {}

        if index in DEMOS:

            signals = [('input_MIC0', raw)]

            for j in range(len(selected)):

                signals.extend([(f'output_{j}_WDAS', unmasked[j]), (f'output_{j}_masked', enhanced[j])])

            gain = min(1., .95/max(float(np.max(np.abs(v))) for _, v in signals))

            for name, v in signals:

                path = OUT / 'audio' / f"{record['scene_id']}_{name}.wav"

                sf.write(path, v*gain, 16000, subtype='FLOAT'); audio[name] = str(path.relative_to(OUT))

                demo_data[f'{index}_{name}'] = v.astype(np.float32)

            audio['common_playback_gain'] = gain

            demo_data[f'{index}_votes'] = votes.astype(np.float32)

            demo_data[f'{index}_raw_phat_maps'] = maps.astype(np.float32)

            demo_data[f'{index}_selected_masks'] = masks[selected]

        scenes.append(dict(index=index, scene_id=record['scene_id'], scenario=record['scenario'], room_id=record['room_id'],

            pcm_replay_sha256=info['pcm_sha256'], candidate_count=c, reconstructed_candidate_max_error=error_direction,

            observed_cache_max_error=feature_error, physical88_cache_max_error=physical_error, detection_cache_max_error=detection_error, validity_max_error_vs_GPU=logit_error, mask_max_error_vs_GPU=mask_error,

            component_masked_closure_max_error=closure_error, truth_directions=truth, output_directions=actual,

            selected_candidates=selected, scores=[float(probabilities[j]) for j in selected], metrics=metrics,

            enhancement=scene_matches, audio=audio,

            timings_ms=dict(physical_proposals=1000*(after_proposals-begin),

                observed_weights_all_candidate_WDAS_TF=1000*(after_observations-after_proposals),

                neural_decode=1000*(after_neural-after_observations), selected_mask_OLA=1000*(after_mask-after_neural),

                total_compute=1000*(after_mask-begin))))

        if (ordinal+1) % 8 == 0:

            print(dict(stage='PHYSICS_VALIDITY_MASK_FULL_REPLAY', completed=ordinal+1, total=64,

                       elapsed_seconds=time.perf_counter()-started), flush=True)

    detection = aggregate([r['metrics'] for r in scenes]); compare(detection, summary['detection'])

    timings = {k: dict(median=float(np.median([r['timings_ms'][k] for r in scenes])),

        p95=float(np.percentile([r['timings_ms'][k] for r in scenes], 95))) for k in scenes[0]['timings_ms']}

    fields = ('SIR_gain_vs_input_db', 'SIR_mask_gain_vs_WDAS_db', 'SI_SDR_gain_vs_input_db',

        'SI_SDR_mask_gain_vs_WDAS_db', 'target_power_transfer_vs_input_db', 'target_mask_power_transfer_vs_WDAS_db',

        'background_attenuation_vs_MIC0_db', 'background_mask_attenuation_vs_WDAS_db',

        'other_speech_attenuation_vs_MIC0_db', 'other_speech_mask_attenuation_vs_WDAS_db')

    enhanced_summary = {k: statistics([r[k] for r in matched if r[k] is not None]) for k in fields}

    result = dict(status='ONE_AUTHORIZED_TRAINING_AND_FULL_PC_MASK_CHAIN_EVALUATED_NOT_ACCEPTED',

        detection=detection, enhancement=enhanced_summary, matched_output_streams=len(matched), total_output_streams=len(outputs),

        interpretation=f"Enhancement conditional on{len(matched)} matched outputs;{detection['fn']}misses and{detection['fp']}false outputs excluded from conditional means; no ASR/word recognition",

        timings_ms=timings, model_budget=split.model_budget(model), selected_epoch=checkpoint['detector_epoch'], selected_enhancer_epoch=checkpoint['enhancer_epoch'], selected_threshold=threshold,

        observation_seconds=ref.N/ref.FS, functional_acceptance=False, reused_development_CAL=True,

        independent_generalization_completed=False, new_training_runs_in_replay=0, training_runs_total=1,

        optimizer_steps_total=dict(detector=384,enhancer=read(run.OUT/'training_history.json')['enhancer_steps']), new_TEST_runs=0, formal_builds=0, board_measurements=0,

        protected_files_unchanged=len(proposal['protected_sha256']), elapsed_replay_seconds=time.perf_counter()-started)

    save(OUT, 'scene_results.json', scenes); save(OUT, 'enhancement_results.json', matched)

    save(OUT, 'all_output_component_results.json', outputs); save(OUT, 'summary.json', result)

    np.savez_compressed(OUT / 'enhancement_energy_evidence.npz', energy=np.asarray(energy_rows),

        columns=np.array(['input_target', 'input_residual', 'WDAS_target', 'WDAS_residual', 'masked_target', 'masked_residual']))

    np.savez_compressed(OUT / 'fixed_scene_evidence.npz', **demo_data)

    assert run.state_sha(model) == state_before

    run.verify_proposal(); run.prior.preparation.verify_prior()

    for path, expected in external.items():

        assert sha(Path(path)) == expected

    save(OUT, 'replay_verification.json', dict(failed=0, original_PCM_replayed_windows=64,

        actual_observed_features_equal_frozen_cache=True, actual_selected_candidates_equal_trained_exports=True,

        no_truth_in_inference=True, same_predicted_mask_applied_to_components=True, weights_unchanged=True,

        source_sha256=sha(Path(__file__)), result_sha256={str(p.relative_to(OUT)): sha(p) for p in OUT.rglob('*') if p.is_file()}))

    assert sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()) < 48*1024**2

    print(result, flush=True)





if __name__ == '__main__':

    main()

