"""PC-only additive enhancement branch. Frozen C16 detector is not modified.

Every fresh confirmed track gets a separate output on the same audio interval.
The older enhancement weights/WDAS remain the reference, not a PL port or a
claim of blind source separation. No clean component or true source count enters.
"""
from pathlib import Path
import hashlib
import importlib
import json
import math
import sys
import time

PACKAGE = Path(__file__).resolve().parents[2]
ROOT = PACKAGE.parents[1]
LEGACY = ROOT / 'results/multisource_equivalent_pc_package_20261009'
for path in (LEGACY, ROOT / 'python_tools'):
    if str(path) in sys.path:
        sys.path.remove(str(path))
    sys.path.insert(0, str(path))

import numpy as np
import torch
from research import directional_tf_mask as tf
from research.multisource_waveform_quality_tuning import ConservativeEnhancementNetwork, apply_mask
from research.frozen_incremental_pc_20261015 import FrozenIncrementalPCDetector, VERSION
from research.retained_pc_detector import normalized_pcm, FS, WINDOW, DURATION


def _preserved_physical_helpers():
    """Load once during startup, then restore the host module/path namespace.

    Legacy geometry_convention/config use different APIs from the current PC
    tree. Retain module objects without replacing current detector imports.
    This startup loader must run before launching any worker threads.
    """
    names = ('config','geometry_convention','gcc_phat_features','pair_quality',
             'runtime_physics','speech_enhancement','speech_enhancement.beamforming_convention')
    prior = {name:sys.modules.get(name) for name in names}
    before = set(sys.modules)
    path = sys.path.copy()
    try:
        for name in names:
            sys.modules.pop(name,None)
        sys.path.insert(0,str(LEGACY))
        return tuple(importlib.import_module(name) for name in
            ('gcc_phat_features','pair_quality','speech_enhancement.beamforming_convention','runtime_physics'))
    finally:
        sys.path[:] = path
        for name in set(sys.modules)-before:
            file = getattr(sys.modules[name],'__file__',None)
            if file and Path(file).resolve().is_relative_to(LEGACY.resolve()):
                sys.modules.pop(name,None)
        for name, module in prior.items():
            if module is None:sys.modules.pop(name,None)
            else:sys.modules[name]=module


gcc, quality, weights_reference, physics = _preserved_physical_helpers()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class PerSourceEnhancementReference:
    """Bounded one-window reference; no selection of a single target or new IDs."""
    def __init__(self):
        descriptor = json.loads((LEGACY / 'model.json').read_text(encoding='utf-8'))
        if sha(LEGACY / 'best_checkpoint.pt') != descriptor['original_checkpoint_sha256']:
            raise ValueError('Preserved enhancement checkpoint changed')
        for name in ('research/multisource_waveform_quality_tuning.py', 'research/multisource_split_network.py',
                     'research/directional_tf_mask.py'):
            expected = descriptor['copied_source_sha256'][name]['sha256']
            if sha(ROOT / 'python_tools' / name) != expected:
                raise ValueError('Enhancement reference source changed: ' + name)
        # 显式绑定旧物理辅助模块，不能替换成新版检测器。
        for module, filename in ((gcc,'gcc_phat_features.py'), (quality,'pair_quality.py'),
                                 (physics,'runtime_physics.py')):
            if Path(module.__file__).resolve() != (LEGACY / filename).resolve():
                raise ValueError('Ambiguous enhancement physical helper: ' + filename)
        state = torch.load(LEGACY / 'best_checkpoint.pt',map_location='cpu',weights_only=True)['state_dict']
        self.model = ConservativeEnhancementNetwork().cpu().eval()
        self.model.load_state_dict({k[len('enhancer.'):]:v for k,v in state.items() if k.startswith('enhancer.')},strict=True)
        self.model.requires_grad_(False)
        self.checkpoint_sha256 = descriptor['original_checkpoint_sha256']
        self.parameters = sum(p.numel() for p in self.model.parameters())
        if self.parameters != 2809:
            raise ValueError('Expected unchanged 2,809-parameter enhancement branch')

    def enhance_pool(self, pcm, quiet_reference, *, candidate_directions, candidate_valid,
                     tracks, audio_interval_s):
        """All candidates provide observed competition; only current tracks output.

        IDs and directions are estimates from the caller, never truth/labels.
        Competition features use the complete valid pool, not just output tracks.
        """
        started = time.perf_counter()
        if np.shares_memory(np.asarray(pcm),np.asarray(quiet_reference)):
            raise ValueError('Independent background reference required')
        frames = normalized_pcm(pcm,((WINDOW,16),(20,2048,16))).reshape(20,2048,16)
        quiet = normalized_pcm(quiet_reference,((8192,16),(4,2048,16))).reshape(4,2048,16)
        directions = np.asarray(candidate_directions,dtype=float)
        valid = np.asarray(candidate_valid)
        if directions.shape != (16,2) or valid.shape != (16,) or not np.isin(valid,[0,1]).all() or not np.isfinite(directions).all():
            raise ValueError('Complete fixed C16 observed pool required')
        interval = list(map(float,audio_interval_s))
        if len(interval)!=2 or not all(math.isfinite(v) for v in interval) or abs(interval[1]-interval[0]-DURATION)>1e-6:
            raise ValueError('One exact observation interval required')
        ids, selected, current, held = set(), [], [], []
        candidates = np.flatnonzero(valid)
        for track in tracks:
            identity = track['source_id']
            if type(identity) is not int or identity<1 or identity in ids:
                raise ValueError('Unique positive anonymous source IDs required')
            ids.add(identity)
            if track.get('confirmed_this_cycle') is not True:
                held.append(identity)
                continue
            direction = np.asarray(track['direction'],float)
            if direction.shape!=(2,) or not np.isfinite(direction).all() or not .65<=float(track['score'])<=1:
                raise ValueError('Fresh valid estimated direction and score required')
            if not len(candidates):
                raise ValueError('Confirmed source without a current candidate')
            delta = np.linalg.norm(directions[candidates]-direction,axis=1)
            index = int(candidates[np.argmin(delta)])
            if min(delta)>1e-4 or index in selected:
                raise ValueError('Current source must map uniquely to its observed candidate')
            selected.append(index)
            current.append(dict(source_id=identity,direction=direction.tolist(),candidate_index=index))
        if len(ids)>8:
            raise ValueError('Frozen track capacity exceeded')
        samples = len(tf.resample(frames.reshape(WINDOW,16)[:,0]))
        if not current:
            return dict(streams=[],held_source_ids=held,enhancement_seconds=time.perf_counter()-started,
                        audio_interval_s=interval,output_sample_rate=16000,output_samples=samples,
                        checkpoint_sha256=self.checkpoint_sha256,continuous_stream=False,board_execution=False)
        if np.any(np.abs(directions[candidates])>=90):
            raise ValueError('Enhancement reference requires front-hemisphere candidates')
        channel_weights = physics.channel_weights(frames,gcc,quality,weights_reference)
        beams = [physics.pc_wdas(frames,d,channel_weights,weights_reference)[0] for d in directions[candidates]]
        features,_,spectra = tf.observed_features(frames,directions[candidates],quiet,beams)
        positions = [int(np.flatnonzero(candidates==i)[0]) for i in selected]
        observed = features.astype(np.float16).astype(np.float32)
        with torch.inference_mode():
            masks = self.model(torch.from_numpy(observed[positions])).numpy()
        streams = []
        for track,j,mask in zip(current,positions,masks):
            streams.append(dict(track,wdas=tf.istft(spectra[j],samples),
                enhanced=apply_mask(spectra[j],mask,samples),mask=mask.copy(),
                audio_interval_s=interval.copy(),output_sample_rate=16000))
        return dict(streams=streams,held_source_ids=held,channel_weights=channel_weights,
            enhancement_seconds=time.perf_counter()-started,audio_interval_s=interval,
            output_sample_rate=16000,output_samples=samples,checkpoint_sha256=self.checkpoint_sha256,
            continuous_stream=False,board_execution=False)

    def enhance_cycle(self, pcm, quiet_reference, cycle):
        if cycle['contract_version']!=VERSION or cycle['sample_rate_hz']!=FS:
            raise ValueError('Exact frozen detector contract required')
        return self.enhance_pool(pcm,quiet_reference,candidate_directions=cycle['observed_directions'],
            candidate_valid=cycle['candidate_valid'],tracks=cycle['tracks'],audio_interval_s=cycle['audio_interval_s'])


class FrozenPerSourcePCReference:
    """Additive integration; detector output remains available without alteration."""
    def __init__(self, *, pose_key):
        self.detector = FrozenIncrementalPCDetector(pose_key=pose_key)
        self.enhancer = PerSourceEnhancementReference()

    def infer_window(self, pcm, quiet_reference, **metadata):
        try:
            cycle = self.detector.infer_window(pcm,quiet_reference,**metadata)
            audio = self.enhancer.enhance_cycle(pcm,quiet_reference,cycle)
        except Exception:
            self.detector.reset_segment()
            raise
        return dict(detection=cycle,source_audio=audio,status='PC_WINDOW_REFERENCE_NOT_CONTINUOUS_OR_BOARD_ACCEPTANCE')
