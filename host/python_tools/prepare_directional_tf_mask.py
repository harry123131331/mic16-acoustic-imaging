"""Prepare one direction-mask trial from the existing320 recipes, no training."""
import os
for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
    os.environ[name] = "2"
from pathlib import Path
import sys
import json
import time
import argparse
import numpy as np
from scipy.signal import lfilter
import train_candidate_neighborhood_one as previous
import run_neural_multisource_enhancement_simulation_20261008 as reference
from research import directional_tf_mask as tf
from research.candidate_rescue_synthesis import BACKGROUND_RMS, rms, array_sha

ROOT = previous.ROOT
OUT = ROOT / "results/directional_tf_mask_preparation_20261008"
read, sha, save = previous.read, previous.sha, previous.save
SHAPE = (320, 16, 67, 64)


def verify_existing():
    protected = previous.verify_proposal()["protected_sha256"]
    for directory in (previous.OUT, ROOT / "results/neural_multisource_enhancement_neighborhood_20261008",
                      ROOT / "results/neural_multisource_component_diagnosis_20261008"):
        m = read(directory / "current_result.json")
        assert sha(ROOT / m["report_path"]) == m["report_sha256"]
        for name, expected in m["authoritative_sha256"].items():
            assert sha(directory / name) == expected, name
    return protected


def main():
    assert not OUT.exists(), "Preserve prepared cache; no overwrite"
    started = time.perf_counter()
    protected = verify_existing()
    arrays = previous.base.load_training_arrays()
    records = arrays["records"]
    metadata = read(previous.base.SUP / "supervision_metadata.json")
    recipes = read(previous.base.DATA / "scene_plan.json")
    with np.load(previous.base.DATA / "feature_cache.npz") as data:
        references = data["quiet_references"].copy()
    speech_lock_path = ROOT / "results/candidate_speech_intake_dev_other_20261007/speech_assets_lock.json"
    rir_lock_path = ROOT / "results/candidate_rir_preparation_20261007/rir_assets_lock.json"
    speech_lock, rir_lock = read(speech_lock_path), read(rir_lock_path)
    for row in speech_lock["speech_assets"]:
        assert sha(Path(row["path"])) == row["sha256"]
    speech, _ = reference.load_speech(speech_lock["speech_assets"])
    rirs = {}
    for row in rir_lock["rir_assets"]:
        assert sha(Path(row["path"])) == row["sha256"]
        with np.load(row["path"]) as data:
            rirs[row["scene_id"]] = data["rir"].copy()
    gcc, quality, weight, external = reference.load_weight_reference()
    OUT.mkdir()
    inputs = [previous.base.DATA / name for name in ("feature_cache.npz", "window_records.json", "scene_plan.json")]
    inputs += [previous.base.SUP / name for name in ("selection_supervision_v2.npz", "supervision_metadata.json")]
    inputs += [speech_lock_path, rir_lock_path, previous.OUT / "best_checkpoint.pt", previous.OUT / "training_history.json"]
    protocol = dict(status="FROZEN_BEFORE_EXISTING_RECIPE_FEATURE_EXTRACTION_NO_TRAINING", date="2026-10-08",
        model_version=tf.VERSION, feature_names=list(tf.FEATURE_NAMES), shape=list(SHAPE)+[10],
        audio_resample="39,062.5Hz ->16kHz,256/625", STFT=dict(fft=512, periodic_Hann=True, hop=256,
            frames=67, band_bins="1..256 grouped four bins; DC uses first mask band", bands=64),
        observed_inputs="PCM+estimated physical directions+independent quiet reference+mixture-derived WDAS beams only",
        target="Assigned ON speech anchor component amplitude IRM sqrt(Ptarget/(Pbackground+sum Pcomponents)) at same estimated beam; negatives0; true-short-gray ignored",
        cross_terms="IRM denominator uses independent powers; not phase-sensitive optimal mask or oracle steering",
        storage="FP16 observation/target/weight cache only; promoted to FP32 for training/inference; quantization errors recorded",
        inference="shared causal TF network -> validity score +64-band mask; actual selected-direction WDAS spectrum masked and overlap-added",
        neural_causality="TF mask uses current/past STFT frames; validity and physical proposals still use full1.048576s window; streaming not certified",
        source_sha256={str(p.relative_to(ROOT)): sha(p) for p in (Path(__file__), Path(tf.__file__), Path(reference.__file__))},
        input_sha256={str(p.relative_to(ROOT)): sha(p) for p in inputs},
        protected_sha256=protected, external_reference_sha256=external,
        new_scene_assets=0, training_runs=0, optimizer_steps=0, new_TEST_runs=0, output_limit_bytes=512*1024**2)
    save(OUT, "extraction_protocol.json", protocol)
    xcache = np.lib.format.open_memmap(OUT / "observed_features.npy", mode="w+", dtype=np.float16, shape=(*SHAPE, 10))
    target_cache = np.lib.format.open_memmap(OUT / "mask_targets.npy", mode="w+", dtype=np.float16, shape=SHAPE)
    power_cache = np.lib.format.open_memmap(OUT / "spectral_weights.npy", mode="w+", dtype=np.float16, shape=SHAPE)
    xcache[:] = 0; target_cache[:] = 0; power_cache[:] = 0
    evidence = []
    max_identity_error = max_linear_error = max_feature_rounding = max_mask_rounding = max_weight_rounding = 0.
    for i, record in enumerate(records):
        tick = time.perf_counter()
        frames, details, components, _, info = reference.build_scene(recipes[i], speech, rirs)
        assert info["pcm_sha256"] == record["pcm_sha256"] and details == record["source_details"]
        directions = arrays["directions"][i, arrays["valid"][i]].astype(float)
        c = len(directions)
        assert c == record["candidate_count"]
        weights = reference.channel_weights(frames, gcc, quality, weight)
        beams = [reference.pc_wdas(frames, direction, weights, weight)[0] for direction in directions]
        features, spectral_weight, spectra = tf.observed_features(frames, directions, references[record["reference_index"]], beams)
        assert features.shape == (c, 67, 64, 10)
        xcache[i, :c] = features
        power_cache[i, :c] = spectral_weight
        max_feature_rounding = max(max_feature_rounding, float(np.max(np.abs(features-xcache[i, :c].astype(float)))))
        max_weight_rounding = max(max_weight_rounding, float(np.max(np.abs(spectral_weight-power_cache[i, :c].astype(float)))))
        raw = tf.resample(beams[0])
        identity_error = float(np.max(np.abs(tf.apply_mask(spectra[0], np.ones((67, 64)), len(raw))-raw)))
        max_identity_error = max(max_identity_error, identity_error)
        assert identity_error < 1e-12
        # 全部观测特征完成后才从此处开始监督。
        rng = np.random.default_rng(recipes[i]["background_seed"])
        noise = lfilter([1.], [1., -.8], rng.standard_normal((reference.N, 16)), axis=0)
        background = (noise*(BACKGROUND_RMS/rms(noise))).astype(np.float32)
        assert array_sha(background) == info["background_sha256"]
        anchors = {r["anchor_candidate"]: r["source_state_index"] for r in metadata[i]["ranking_targets"]}
        positive_ids = np.flatnonzero(arrays["groups"][i, 0]).tolist()
        assert sorted(anchors) == positive_ids
        states = record["supervision"]["source_states"]
        assert len(states) == len(details)
        component_spectra = np.zeros((c, len(components), 67, 257), complex)
        background_spectra = np.zeros((c, 67, 257), complex)
        anchor_rows = []
        for candidate, source in anchors.items():
            assert states[source]["role"] == details[source]["role"] == "speech"
            assert states[source]["direction"] == details[source]["direction"]
            assert details[source]["active_samples"] >= 7813
            for j, component in enumerate(components):
                beam, _ = reference.pc_wdas(component, directions[candidate], weights, weight)
                component_spectra[candidate, j] = tf.stft(tf.resample(beam))
            bg_beam, _ = reference.pc_wdas(background, directions[candidate], weights, weight)
            background_spectra[candidate] = tf.stft(tf.resample(bg_beam))
            closure = component_spectra[candidate].sum(0)+background_spectra[candidate]-spectra[candidate]
            max_linear_error = max(max_linear_error, float(np.max(np.abs(closure))))
            anchor_rows.append(dict(candidate=candidate, component_source_index=source,
                source_direction=details[source]["direction"], estimated_direction=directions[candidate].tolist(),
                angular_error_deg=next(r["anchor_error_deg"] for r in metadata[i]["ranking_targets"] if r["anchor_candidate"] == candidate)))
        target = tf.component_mask_targets(component_spectra, background_spectra, anchors, c)
        target_cache[i, :c] = target
        max_mask_rounding = max(max_mask_rounding, float(np.max(np.abs(target-target_cache[i, :c].astype(float)))))
        assert not np.any(target[~arrays["groups"][i, 0, :c]])
        evidence.append(dict(index=i, role=record["role"], scene_id=record["scene_id"],
            original_PCM_sha256=info["pcm_sha256"], candidate_count=c, assigned_speech_anchors=anchor_rows,
            mask_target_nonzero_bins=int(np.count_nonzero(target)),
            extracted_seconds=time.perf_counter()-tick, identity_mask_max_error=identity_error))
        if (i+1) % 16 == 0:
            elapsed = time.perf_counter()-started
            print(json.dumps(dict(stage="EXISTING_RECIPES_OBSERVED_TF_AND_SUPERVISION", completed=i+1, total=320,
                elapsed_seconds=elapsed, remaining_seconds_linear_estimate=elapsed/(i+1)*(320-i-1))), flush=True)
    xcache.flush(); target_cache.flush(); power_cache.flush()
    del xcache, target_cache, power_cache
    assert max_linear_error < 1e-5
    np.savez_compressed(OUT / "selection_arrays.npz", labels=arrays["labels"], candidate_valid=arrays["valid"],
        directions=arrays["directions"], group_masks=arrays["groups"], ranking_pairs=arrays["ranking_pairs"],
        train_indices=arrays["train_indices"], cal_indices=arrays["cal_indices"])
    save(OUT, "input_supervision_evidence.json", evidence)
    summary = dict(status="OBSERVED_TF_INPUT_AND_MASK_SUPERVISION_PREPARED_NO_TRAINING",
        windows=320, TRAIN=256, development_CAL=64, feature_shape=list(SHAPE)+[10],
        speech_anchor_count=sum(len(r["assigned_speech_anchors"]) for r in evidence),
        TRAIN_anchors=int(arrays["groups"][arrays["train_indices"], 0].sum()),
        CAL_anchors=int(arrays["groups"][arrays["cal_indices"], 0].sum()),
        model_budget=tf.model_budget(tf.DirectionalTFMask()), preparation_elapsed_seconds=time.perf_counter()-started,
        maximum_identity_reconstruction_error=max_identity_error,
        maximum_component_STFT_linearity_error=max_linear_error,
        maximum_feature_FP16_rounding_error=max_feature_rounding,
        maximum_mask_FP16_rounding_error=max_mask_rounding,
        maximum_spectral_weight_FP16_rounding_error=max_weight_rounding,
        training_runs=0, optimizer_steps=0, new_scene_assets=0, new_TEST_runs=0,
        acceptance=False, board_measurements=0, formal_builds=0)
    save(OUT, "input_summary.json", summary)
    for section in ("source_sha256", "input_sha256", "protected_sha256"):
        for path, expected in protocol[section].items():
            assert sha(ROOT / path) == expected, path
    for path, expected in external.items():
        assert sha(Path(path)) == expected
    verify_existing()
    assert sum(p.stat().st_size for p in OUT.rglob("*") if p.is_file()) < protocol["output_limit_bytes"]
    save(OUT, "input_verification.json", dict(failed=0, reconstructed_existing_PCM_windows=320,
        observed_feature_function_has_no_truth_arguments=True, component_supervision_evaluated_after_observations=True,
        same_estimated_direction_and_mixture_weights_for_all_components=True,
        identity_mask_reconstruction_passed=True, original_377_anchor_mapping_preserved=True,
        training_runs=0, optimizer_steps=0, protected_files_unchanged=len(protected),
        result_sha256={p.name: sha(p) for p in OUT.iterdir() if p.is_file()}))
    print(json.dumps(summary, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare", action="store_true")
    args = parser.parse_args()
    if args.prepare:
        main()
    else:
        print(json.dumps(dict(status="READ_ONLY_NO_PREPARATION_OR_TRAINING_REQUESTED", prepared=OUT.exists())))
