"""Replay frozen CAL through physical candidates, trained TCN and PC WDAS.

No new scenes, training, threshold fitting, TEST access or formal compilation.
All source components are kept OUT of inference and used only after output
selection for component SIR evaluation. This is floating PC simulation.
"""
from __future__ import annotations
import os
for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
    os.environ[name] = "2"
from pathlib import Path
import importlib
import json
import sys
import time
import types
import numpy as np
import soundfile as sf
import torch
from scipy.signal import lfilter, resample_poly

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/verification"))
import train_candidate_joint_selection_v2_one as seals
import prepare_candidate_rescue_windows as front
from research.candidate_rescue_synthesis import load_speech, build_scene, array_sha, FS, N
from research.candidate_rescue_features import extract_candidate_features
from research.candidate_joint_validity_network import CandidateJointValidityTCN, decode_joint_validity, model_budget
from acoustic_camera.geometry_2d import far_field_arrival_delays_samples_azel
from acoustic_camera.tf_zone_histogram_2d import TfZoneProjector
from acoustic_camera.tf_zone_neural_features import extract_neural_features
from audit_pre_gate_candidate_coverage import clusters, fixed_bounded_pool
from mic16_config import Mic16Config

OUT = ROOT / "results/neural_multisource_enhancement_simulation_20261008"
read, sha, save = seals.read, seals.sha, seals.save


def load_weight_reference():
    source = ROOT.parent / "ssl_nn"
    names = ("config.py", "geometry_convention.py", "gcc_phat_features.py", "pair_quality.py", "speech_enhancement/beamforming_convention.py")
    manifest = read(ROOT / "results/pc_revalidation_20260909/test/reproduction_manifest.json")
    for name in names:
        assert sha(source / name).upper() == manifest["source_sha256"][name].upper(), name
    sys.path.insert(0, str(source))
    package = types.ModuleType("speech_enhancement")
    package.__path__ = [str(source / "speech_enhancement")]
    sys.modules["speech_enhancement"] = package
    gcc = importlib.import_module("gcc_phat_features")
    quality = importlib.import_module("pair_quality")
    weight = importlib.import_module("speech_enhancement.beamforming_convention")
    return gcc, quality, weight, {str(source / name): sha(source / name) for name in names}


def channel_weights(frames, gcc, quality, weight):
    """Only observed PCM enters per-frame dynamic weights; no truth or K."""
    result = []
    for frame in frames:
        curves = gcc.extract_gcc_phat_features(frame, flatten=False)
        reliability, _ = quality.compute_pair_reliability(curves)
        result.append(weight.pair_reliability_to_channel_weights(reliability))
    result = np.asarray(result)
    assert result.shape == (20, 16) and np.isfinite(result).all() and np.allclose(result.sum(1), 1, atol=1e-6)
    return result


def pc_wdas(pcm, direction, weights, weight_reference):
    """Causal 2D far-field steering; six-tap Lagrange FIR, continuous window.

    The existing frozen PC coefficient/weight rules are used. This function
    does not claim bit-exact C/HLS output or real board scheduling.
    """
    cfg = Mic16Config()
    x = np.asarray(pcm, np.float64).reshape(N, 16)
    arrivals = far_field_arrival_delays_samples_azel(*direction, cfg).astype(float)
    common = int(np.ceil(cfg.array_radius_m * cfg.sample_rate / cfg.sound_speed))
    delay = common - arrivals
    integer = np.floor(delay).astype(int)
    aligned = np.zeros_like(x)
    for channel in range(16):
        coefficient = weight_reference.lagrange5_coefficients(float(delay[channel]-integer[channel]))
        filtered = lfilter(coefficient, [1.], x[:, channel])
        d = int(integer[channel])
        aligned[d:, channel] = filtered[:N-d]
    beam = np.sum(aligned * np.repeat(weights, 2048, axis=0), axis=1)
    guard = int(integer.max()) + 5
    return beam, dict(common_delay_samples=common, guard_samples=guard, far_field_azel=True,
                     coefficients="frozen six-tap Lagrange5", precision="PC float, not FPGA fixed-point")


def energies_by_frame(signals, guard):
    valid = np.arange(N) >= guard
    return np.stack([np.sum(np.where(valid, signal*signal, 0).reshape(20, 2048), axis=1) for signal in signals], axis=-1)


def main():
    assert not OUT.exists(), "Preserve existing simulation evidence"
    protected = seals.verify_prior_results()
    seals.verify_proposal()
    gcc, quality, weight, external = load_weight_reference()
    records = read(seals.DATA / "window_records.json")
    recipes = read(seals.DATA / "scene_plan.json")
    cal = [r["index"] for r in records if r["role"] == "CAL"]
    demo_ids = [next(r["index"] for r in records if r["role"] == "CAL" and r["scenario"] == scenario)
                for scenario in ("one", "two", "three", "one_fan", "two_fan", "fan")]
    previous = read(seals.previous.OUT / "CAL_all_threshold_predictions.json")["0.8"]
    by_index = {entry["index"]: entry for entry in previous}
    with np.load(seals.DATA / "feature_cache.npz") as cache:
        cached_x, cached_direction, references = cache["features"], cache["directions"], cache["quiet_references"]
    speech_lock = read(ROOT / "results/candidate_speech_intake_dev_other_20261007/speech_assets_lock.json")
    rir_lock = read(ROOT / "results/candidate_rir_preparation_20261007/rir_assets_lock.json")
    for row in speech_lock["speech_assets"]:
        assert sha(Path(row["path"])) == row["sha256"]
    speech, _ = load_speech(speech_lock["speech_assets"])
    rirs = {}
    for row in rir_lock["rir_assets"]:
        assert sha(Path(row["path"])) == row["sha256"]
        with np.load(row["path"]) as cache:
            rirs[row["scene_id"]] = cache["rir"].copy()
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    model = CandidateJointValidityTCN().eval()
    checkpoint = torch.load(seals.previous.OUT / "best_checkpoint.pt", map_location="cpu", weights_only=True)
    model.load_state_dict(checkpoint["state_dict"])
    with torch.no_grad():
        model(torch.zeros(1, 16, 20, 88))  # 仅预热，不使用优化器。
    projector = TfZoneProjector()
    aa, ee = np.meshgrid(projector.grid.azimuths_deg.astype(float), projector.grid.elevations_deg.astype(float))
    az, el = np.deg2rad(aa.ravel()), np.deg2rad(ee.ravel())
    cosine = np.sin(el[:, None])*np.sin(el[None, :]) + np.cos(el[:, None])*np.cos(el[None, :])*np.cos(az[:, None]-az[None, :])
    allowed = np.rad2deg(np.arccos(np.clip(cosine, -1, 1))) >= 15
    OUT.mkdir(); (OUT / "audio").mkdir()
    save(OUT, "simulation_protocol.json", dict(status="FROZEN_BEFORE_EXISTING_CAL_CHAIN_REPLAY",
        scene_indices=cal, fixed_illustration_indices=demo_ids, illustration_selection="first CAL of each listed scenario; no performance-based choice",
        checkpoint_sha256=sha(seals.previous.OUT / "best_checkpoint.pt"), threshold=.8, match_deg=10,
        enhancement="Every neural output drives its own PC dynamic WDAS beam; this does not imply parallel FPGA multibeam deployment",
        SIR="matched target component versus all other components+background, before MIC0 and after same estimated-direction WDAS; startup guard excluded",
        no_truth_in_inference=True, external_reference_sha256=external, generator_sha256=sha(Path(__file__)),
        no_new_scene_assets=True, no_training=True, no_new_TEST=True, no_formal_build=True, output_limit_bytes=16*1024**2))
    scene_results, all_energies, enhanced_records = [], [], []
    started = time.perf_counter()
    for ordinal, index in enumerate(cal):
        record = records[index]
        frames, details, components, _, info = build_scene(recipes[index], speech, rirs)
        assert info["pcm_sha256"] == record["pcm_sha256"] and details == record["source_details"]
        begin = time.perf_counter()
        reference = references[record["reference_index"]]
        maps, _ = extract_neural_features(projector, frames, reference)
        votes = front.votes_from_maps(projector, maps, frames, reference, allowed)
        pool = fixed_bounded_pool(clusters(votes, np.ones(480, bool), 10.))
        directions = np.asarray([entry["direction"] for entry in pool])
        features, _ = extract_candidate_features(frames, reference, directions, votes)
        after_front = time.perf_counter()
        with torch.no_grad():
            logits = model(torch.from_numpy(features[None])).numpy()[0]
        probabilities = 1 / (1 + np.exp(-logits.astype(float)))
        output, selected = decode_joint_validity(directions, probabilities, np.ones(len(pool), bool), .8)
        after_neural = time.perf_counter()
        assert len(pool) == record["candidate_count"]
        direction_error = float(np.max(np.abs(directions-cached_direction[index, :len(pool)])))
        feature_error = float(np.max(np.abs(features-cached_x[index, :len(pool)])))
        assert direction_error < 1e-6 and feature_error < 2e-5, (index, direction_error, feature_error)
        assert selected == by_index[index]["selected_candidates"], (index, "outputs changed")
        assert np.allclose(np.asarray(output).reshape(-1, 2), np.asarray(by_index[index]["outputs"]).reshape(-1, 2), atol=1e-6, rtol=0)
        weights = channel_weights(frames, gcc, quality, weight) if output else None
        beams, beam_info = [], []
        for direction in output:
            beam, meta = pc_wdas(frames, direction, weights, weight)
            beams.append(beam); beam_info.append(meta)
        after_enhance = time.perf_counter()
        truths, gray, clear = seals.previous.context(record)
        metrics = dict(seals.previous.score_window(output, truths, gray), truth_count=len(truths), clear_zero=clear)
        assert (metrics["tp"], metrics["fp"], metrics["fn"]) == tuple(by_index[index]["metrics"][key] for key in ("tp", "fp", "fn"))
        source_indices = [j for j, source in enumerate(details) if source["role"] == "speech" and source["active_samples"] >= 7813]
        enhanced = []
        for pair in metrics["pairs"]:
            output_id, truth_id = pair["output"], pair["truth"]
            component = components[source_indices[truth_id]]
            target, _ = pc_wdas(component, output[output_id], weights, weight)
            input_target = component[:, 0].astype(float)
            input_residual = frames.reshape(N, 16)[:, 0].astype(float)-input_target
            residual = beams[output_id]-target
            energy = energies_by_frame((input_target, input_residual, target, residual), beam_info[output_id]["guard_samples"])
            total = energy.sum(axis=0)
            input_sir = float(10*np.log10(total[0]/total[1]))
            output_sir = float(10*np.log10(total[2]/total[3]))
            item = dict(scene_index=index, output_index=output_id, truth_index=truth_id, angular_error_deg=pair["error_deg"],
                direction=output[output_id], input_component_SIR_db=input_sir, output_component_SIR_db=output_sir,
                improvement_db=output_sir-input_sir, energy_record_index=len(all_energies), startup_guard=beam_info[output_id]["guard_samples"])
            all_energies.append(energy); enhanced_records.append(item); enhanced.append(item)
        audio = {}
        if index in demo_ids:
            raw = frames.reshape(N, 16)[:, 0].astype(float)
            gain = min(1., .95/max([float(np.max(np.abs(raw))), *[float(np.max(np.abs(beam))) for beam in beams]]))
            signals = [("input_MIC0", raw)] + [(f"output_{j}", beam) for j, beam in enumerate(beams)]
            for name, values in signals:
                path = OUT / "audio" / f"{record['scene_id']}_{name}.wav"
                sf.write(path, resample_poly(values*gain, 256, 625), 16000, subtype="FLOAT")
                audio[name] = str(path.relative_to(OUT))
            audio["common_playback_gain"] = gain
        scene_results.append(dict(index=index, scene_id=record["scene_id"], scenario=record["scenario"], room_id=record["room_id"],
            pcm_replay_sha256=info["pcm_sha256"], candidate_direction_max_abs_difference=direction_error,
            feature_max_abs_difference=feature_error, truth_directions=truths, output_directions=output,
            selected_candidates=selected, scores=[float(probabilities[j]) for j in selected], metrics=metrics,
            enhancement=enhanced, unmatched_output_count=len(output)-len(metrics["pairs"]), audio=audio,
            timings_ms=dict(physical_frontend=1000*(after_front-begin), neural_decode=1000*(after_neural-after_front),
                actual_output_WDAS=1000*(after_enhance-after_neural), total_compute=1000*(after_enhance-begin))))
        if (ordinal+1) % 8 == 0:
            print(json.dumps(dict(stage="FROZEN_SCENES_PHYSICS_NEURAL_WDAS", completed=ordinal+1, total=64,
                elapsed_seconds=time.perf_counter()-started)), flush=True)
    detection = seals.previous.aggregate([entry["metrics"] for entry in scene_results])
    assert (detection["tp"], detection["fp"], detection["fn"]) == (34, 28, 44)
    gains = np.array([entry["improvement_db"] for entry in enhanced_records])
    timings = {name: dict(median=float(np.median([entry["timings_ms"][name] for entry in scene_results])),
                         p95=float(np.percentile([entry["timings_ms"][name] for entry in scene_results], 95)))
               for name in scene_results[0]["timings_ms"]}
    np.savez_compressed(OUT / "enhancement_energy_evidence.npz", energy_by_frame=np.asarray(all_energies))
    save(OUT, "scene_results.json", scene_results)
    save(OUT, "enhancement_results.json", enhanced_records)
    result = dict(status="PC_PHYSICS_NEURAL_WDAS_CHAIN_REPLAY_COMPLETED", detection=detection,
        enhancement=dict(matched_output_streams=len(gains), total_output_streams=sum(len(row["output_directions"]) for row in scene_results),
            mean_component_SIR_improvement_db=float(gains.mean()), median_component_SIR_improvement_db=float(np.median(gains)),
            minimum_improvement_db=float(gains.min()), positive_improvement_streams=int((gains > 0).sum()),
            interpretation="Conditional on34 matched outputs, excludes44 missed sources and28 unmatched streams; component SIR is not SI-SDR/STOI/PESQ or recognition accuracy"),
        timings_ms=timings, model_budget=model_budget(model), checkpoint_epoch=checkpoint["epoch"], fixed_threshold=.8,
        compute_time_scope="Measured PC algorithm only; excludes replay reconstruction, truth metrics, plotting, audio IO and1.048576s observation wait; no board timing",
        reused_development_CAL=True, independent_generalization_completed=False, board_measurements=0,
        new_training_runs=0, new_scene_assets=0, replayed_existing_windows=64, new_TEST_runs=0, formal_builds=0,
        protected_files_unchanged=len(protected), elapsed_replay_seconds=time.perf_counter()-started)
    save(OUT, "summary.json", result)
    seals.verify_prior_results(); seals.verify_proposal()
    for path, expected in external.items():
        assert sha(Path(path)) == expected
    assert sum(path.stat().st_size for path in OUT.rglob("*") if path.is_file()) < 16*1024**2
    print(json.dumps(result, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
