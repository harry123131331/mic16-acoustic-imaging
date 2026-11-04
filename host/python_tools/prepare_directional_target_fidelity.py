"""Prepare positive-only target spectrum labels from frozen recipes, no training."""
import os
for name in ('OMP_NUM_THREADS', 'MKL_NUM_THREADS', 'OPENBLAS_NUM_THREADS'):
    os.environ[name] = '2'
from pathlib import Path
import argparse
import time
import numpy as np
import train_directional_tf_mask_one as prior
import run_neural_multisource_enhancement_simulation_20261008 as ref
from research import directional_tf_mask as tf
from research import directional_target_fidelity as fidelity

ROOT = prior.ROOT
OUT = ROOT / 'results/directional_target_fidelity_preparation_20261009'
read, sha, save = prior.read, prior.sha, prior.save


def verify_prior():
    protected = prior.verify_proposal()['protected_sha256']
    for directory in (prior.OUT, ROOT / 'results/neural_multisource_enhancement_directional_mask_20261008', prior.PREP):
        manifest = read(directory / 'current_result.json')
        assert sha(ROOT / manifest['report_path']) == manifest['report_sha256']
        for name, expected in manifest['authoritative_sha256'].items():
            assert sha(directory / name) == expected, name
    return protected


def main():
    assert not OUT.exists(), 'Preserve previous labels and evidence'
    tick = time.perf_counter(); protected = verify_prior()
    arrays = prior.load_arrays()
    records = read(prior.preparation.previous.base.DATA / 'window_records.json')
    recipes = read(prior.preparation.previous.base.DATA / 'scene_plan.json')
    metadata = read(prior.preparation.previous.base.SUP / 'supervision_metadata.json')
    speech_path = ROOT / 'results/candidate_speech_intake_dev_other_20261007/speech_assets_lock.json'
    rir_path = ROOT / 'results/candidate_rir_preparation_20261007/rir_assets_lock.json'
    speech_lock, rir_lock = read(speech_path), read(rir_path)
    for row in speech_lock['speech_assets']:
        assert sha(Path(row['path'])) == row['sha256']
    speech, _ = ref.load_speech(speech_lock['speech_assets'])
    rirs = {}
    for row in rir_lock['rir_assets']:
        assert sha(Path(row['path'])) == row['sha256']
        with np.load(row['path']) as data:
            rirs[row['scene_id']] = data['rir'].copy()
    gcc, quality, weight, external = ref.load_weight_reference()
    OUT.mkdir()
    inputs = [prior.PREP / 'observed_features.npy', prior.PREP / 'selection_arrays.npz',
        prior.preparation.previous.base.DATA / 'window_records.json', prior.preparation.previous.base.DATA / 'scene_plan.json',
        prior.preparation.previous.base.SUP / 'supervision_metadata.json', speech_path, rir_path]
    sources = [Path(__file__), Path(tf.__file__), Path(fidelity.__file__), Path(ref.__file__)]
    save(OUT, 'extraction_protocol.json', dict(status='FROZEN_BEFORE_LABEL_ONLY_EXISTING_RECIPE_REPLAY', date='2026-10-09',
        loss_version=fidelity.VERSION, architecture='Unchanged frozen1034-parameter DirectionalTFMask',
        change='Selection loss unchanged; only assigned positive anchors enter complex spectrum reconstruction and target-component distortion; negatives/gray/padding do not receive enhancement supervision',
        formula='Lenh=sum|mY-S|²/E_S +0.25sum|(1-m)S|²/E_S; sum all67x257 observed bins; DC shares first64-band mask',
        aggregation='mean anchors within positive window, then mean positive windows; selection keeps original equally weighted supervised windows',
        normalization='E_S=sum|S|² before predicted mask; target energy in labels only; no predicted-output normalization',
        mixture='same estimated candidate direction and observed dynamic weights; phase/coherent cross terms retained',
        mask_range=[0, 1], phase_correction=False, absolute_amplitude_fidelity=True,
        caveat='Additional .25preservation weight is an engineering choice frozen before label replay; not fitted to CAL or claimed optimal; neither spectral MSE nor preservation guarantees perceptual quality',
        observations='Reuse hash-locked FP16 feature cache promoted FP32, no new observed features/candidate directions',
        references=['https://www.erdogan.org/publications/erdogan15icassp.pdf', 'https://link.springer.com/article/10.1186/s13636-021-00207-6'],
        source_sha256={str(p.relative_to(ROOT)): sha(p) for p in sources},
        input_sha256={str(p.relative_to(ROOT)): sha(p) for p in inputs}, protected_sha256=protected,
        external_reference_sha256=external, new_scene_assets=0, new_TEST_runs=0, training_runs=0, optimizer_steps=0,
        output_limit_bytes=32*1024**2))
    coefficients, rows = [], []
    index_map = np.full((320, 16), -1, np.int32)
    maximum_formula_error = maximum_FP32_formula_error = maximum_coefficient_rounding = 0.
    mask = np.random.default_rng(20261009).uniform(.1, .95, (67, 64))
    full_mask = np.c_[mask[:, :1], np.repeat(mask, 4, axis=1)]
    for i, record in enumerate(records):
        frames, details, components, _, info = ref.build_scene(recipes[i], speech, rirs)
        assert info['pcm_sha256'] == record['pcm_sha256'] and details == record['source_details']
        weights = ref.channel_weights(frames, gcc, quality, weight)
        anchors = metadata[i]['ranking_targets']
        assert sorted(r['anchor_candidate'] for r in anchors) == np.flatnonzero(arrays['group_masks'][i, 0]).tolist()
        for row in anchors:
            candidate, source = row['anchor_candidate'], row['source_state_index']
            assert details[source]['role'] == 'speech' and details[source]['active_samples'] >= 7813
            direction = arrays['directions'][i, candidate].astype(float)
            # 先完成观测混合波束，再接入目标分量标签。
            y = tf.stft(tf.resample(ref.pc_wdas(frames, direction, weights, weight)[0]))
            s = tf.stft(tf.resample(ref.pc_wdas(components[source], direction, weights, weight)[0]))
            c, target_energy = fidelity.reconstruction_coefficients(y, s)
            direct = float(np.sum(np.abs(full_mask*y-s)**2)/target_energy)
            represented = float(np.sum(c[..., 0]*mask*mask-2*c[..., 1]*mask+c[..., 2]))
            fp32 = c.astype(np.float32)
            represented32 = float(np.sum(fp32[..., 0]*mask*mask-2*fp32[..., 1]*mask+fp32[..., 2]))
            maximum_formula_error = max(maximum_formula_error, abs(direct-represented))
            maximum_FP32_formula_error = max(maximum_FP32_formula_error, abs(direct-represented32))
            maximum_coefficient_rounding = max(maximum_coefficient_rounding, float(np.max(np.abs(fp32.astype(float)-c))))
            assert abs(direct-represented) < 1e-10 and abs(direct-represented32) < 1e-5
            assert abs(float(fp32[..., 2].sum())-1) < 2e-7
            index_map[i, candidate] = len(coefficients); coefficients.append(fp32)
            rows.append(dict(coefficient_index=len(rows), scene_index=i, role=record['role'], scene_id=record['scene_id'],
                candidate=candidate, source_component_index=source, estimated_direction=direction.tolist(),
                original_PCM_sha256=info['pcm_sha256'], target_energy=target_energy,
                independent_complex_error=direct, coefficient_complex_error=represented,
                FP32_coefficient_complex_error=represented32))
        if (i+1) % 32 == 0:
            print(dict(stage='EXISTING_PCM_POSITIVE_TARGET_LABELS_ONLY', completed=i+1, total=320,
                       elapsed_seconds=time.perf_counter()-tick), flush=True)
    assert len(coefficients) == 377 and np.array_equal(index_map >= 0, arrays['group_masks'][:, 0])
    np.save(OUT / 'positive_reconstruction_coefficients.npy', np.stack(coefficients))
    np.savez_compressed(OUT / 'anchor_index_map.npz', index_map=index_map)
    save(OUT, 'label_evidence.json', rows)
    save(OUT, 'input_summary.json', dict(status='TARGET_FIDELITY_SUPERVISION_PREPARED_NO_TRAINING',
        windows=320, TRAIN_windows=256, CAL_windows=64, TRAIN_anchors=sum(r['role'] == 'TRAIN' for r in rows),
        CAL_anchors=sum(r['role'] == 'CAL' for r in rows), coefficient_shape=[377, 67, 64, 3], coefficient_precision='FP32',
        maximum_complex_formula_error=maximum_formula_error, maximum_FP32_complex_formula_error=maximum_FP32_formula_error,
        maximum_coefficient_FP32_rounding_error=maximum_coefficient_rounding,
        identical_observations_reused=True, old_377_anchor_mapping_preserved=True,
        input_preparation_elapsed_seconds=time.perf_counter()-tick,
        new_observed_features=0, new_scene_assets=0, new_TEST_runs=0, training_runs=0, optimizer_steps=0,
        model_budget=fidelity.model_budget(fidelity.DirectionalTFMask()), functional_acceptance=False))
    protocol = read(OUT / 'extraction_protocol.json')
    for key in ('source_sha256', 'input_sha256', 'protected_sha256'):
        for path, expected in protocol[key].items():
            assert sha(ROOT / path) == expected, path
    for path, expected in external.items():
        assert sha(Path(path)) == expected
    verify_prior()
    save(OUT, 'input_verification.json', dict(failed=0, original_PCM_reconstructed_windows=320,
        preserved_positive_anchors=377, negative_gray_padding_anchors=0,
        target_and_mixture_same_estimated_direction_and_weights=True, full257_bins_including_DC=True,
        complex_cross_phase_retained=True, training_runs=0, optimizer_steps=0,
        protected_files_unchanged=len(protected), result_sha256={p.name: sha(p) for p in OUT.iterdir() if p.is_file()}))
    assert sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()) < 32*1024**2
    print(read(OUT / 'input_summary.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--prepare-labels', action='store_true')
    if parser.parse_args().prepare_labels:
        main()
    else:
        print(dict(status='READ_ONLY_NO_PREPARATION_OR_TRAINING_REQUESTED', prepared=OUT.exists(), new_training_runs=0))
