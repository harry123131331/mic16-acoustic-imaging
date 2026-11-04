"""Isolated PC entry for the fixed 29,441-parameter research candidate.

No truth, class, speaker or known source-count argument enters inference.
The stable geometry scorer and original physical front end remain unchanged.
"""
from pathlib import Path
import hashlib
import json
import math
import time
import numpy as np
import torch
from research.meeting_room_detection_features import MeetingRoomFeatures
from research.candidate_subband_competition import CompetitionNetwork, subband_contrasts
from research.stable_candidate_runtime import stable_score
from research.candidate_direction_decode import decode

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT/'results/fixed_geometry_candidate_publication_20261014/runtime_candidate_manifest.json'
MANIFEST_SHA = '70f48100e6834a305d670d5cd1c9fe997948f0994737178a27a3211e816d9a3f'
VERSION = 'retained_subband_C16_PC_entry_20261014'
FS = 39062.5
WINDOW = 40960
REFERENCE = 8192
DURATION = WINDOW/FS
ORIGINS = ('SIMULATED_REPLAY', 'FILE_REPLAY', 'LIVE_PCM_UNVERIFIED')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def normalized_pcm(values, shapes):
    x = np.asarray(values)
    if x.shape not in shapes or not np.issubdtype(x.dtype, np.floating):
        raise ValueError('须为指定形状的已归一化浮点16路PCM；不能隐式转换整数原始包')
    if not np.isfinite(x).all() or (x.size and np.max(np.abs(x)) > 1):
        raise ValueError('PCM须为有限值且幅值在[-1,1]内')
    return x.astype(np.float32, copy=False)


def physical_heatmap(votes):
    """Same unweighted six-degree vote-density view used by prior evaluation."""
    from audit_pre_gate_candidate_coverage import distances
    az = np.arange(-60, 61, 5); el = np.arange(-30, 31, 5)
    aa, ee = np.meshgrid(az, el)
    grid = np.c_[aa.ravel(), ee.ravel()]
    density = np.exp(-.5*(distances(grid, votes[:, 4:6])/6.)**2).sum(1)
    relative = 10*np.log10(np.maximum(density/max(float(density.max()), 1e-30), 1e-3))
    return dict(azimuths_deg=az.tolist(), elevations_deg=el.tolist(),
        relative_vote_density_db=relative.reshape(len(el), len(az)).tolist(),
        description='观测票的6°高斯密度，各窗按最大值归一化，底限-30dB',
        calibrated_SPL=False, neural_probability=False, votes_count=len(votes))


class RetainedPCDetector:
    def __init__(self, *, audit_capture=False):
        if sha(MANIFEST) != MANIFEST_SHA:
            raise ValueError('固定候选清单发生变化')
        self.manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
        m = self.manifest
        if (sha(ROOT/m['weights']) != m['weights_sha256'] or
                sha(ROOT/m['runtime_source']) != m['runtime_source_sha256'] or
                m['score_threshold'] != .65 or m['NMS_deg'] != 15. or m['formal_release']):
            raise ValueError('权重、几何评分代码或固定解码参数不匹配')
        # 从保留清单绑定全部原前端及解码依赖。
        development = ROOT/'results/ordinary_subband_duplicate_alignment_20261014/development_candidate_manifest.json'
        if sha(development) != m['original_development_manifest_sha256']:
            raise ValueError('开发候选来源清单不匹配')
        for name, expected in json.loads(development.read_text(encoding='utf-8'))['source_sha256'].items():
            if sha(ROOT/name) != expected:
                raise ValueError('冻结算法依赖不匹配: '+name)
        self.model = CompetitionNetwork().cpu().eval()
        self.model.load_state_dict(torch.load(ROOT/m['weights'], map_location='cpu', weights_only=True)['state_dict'], strict=True)
        self.model.requires_grad_(False)
        if sum(p.numel() for p in self.model.parameters()) != 29441:
            raise ValueError('网络参数数目不匹配')
        self.frontend = MeetingRoomFeatures()
        self.audit_capture = bool(audit_capture)
        self.last_observed = None
        self.forward_calls = 0

    def infer_window(self, pcm, quiet_reference, *, sample_rate, audio_start_s,
                     audio_end_s, data_origin, contiguous):
        self.last_observed = None
        if sample_rate != FS or data_origin not in ORIGINS or contiguous is not True:
            raise ValueError('须明确39062.5Hz、输入来源及连续完整窗')
        if not all(math.isfinite(t) for t in (audio_start_s, audio_end_s)) or abs(audio_end_s-audio_start_s-DURATION)>1e-6:
            raise ValueError('音频时间戳须对应完整1.048576秒窗')
        if np.shares_memory(np.asarray(pcm), np.asarray(quiet_reference)):
            raise ValueError('静音参考不能与当前声窗共享数据')
        x = normalized_pcm(pcm, ((WINDOW,16),(20,2048,16))).reshape(20,2048,16)
        n = normalized_pcm(quiet_reference, ((REFERENCE,16),(4,2048,16))).reshape(4,2048,16)
        tick = time.perf_counter()
        obs = self.frontend.extract(x, n)
        front_s = time.perf_counter()-tick; tick = time.perf_counter()
        contrast = subband_contrasts(obs['features'][None], obs['directions'][None], obs['valid'][None])
        features = np.concatenate((obs['features'][None], contrast), -1)
        if features.shape != (1,16,20,130) or not np.isfinite(features).all():
            raise ValueError('130维观测特征接口错误')
        contrast_s = time.perf_counter()-tick; tick = time.perf_counter()
        with torch.inference_mode():
            logits = stable_score(self.model, torch.from_numpy(features),
                torch.from_numpy(obs['directions'][None]), torch.from_numpy(obs['valid'][None]))[0].numpy()
        self.forward_calls += 1
        net_s = time.perf_counter()-tick; tick = time.perf_counter()
        scores = 1/(1+np.exp(-logits.astype(float)))
        sources = decode(obs['directions'], scores, obs['valid'], 15., .65)
        decode_s = time.perf_counter()-tick; tick = time.perf_counter()
        heatmap = physical_heatmap(obs['votes'])
        heatmap_s = time.perf_counter()-tick
        if self.audit_capture:
            self.last_observed = dict(obs, augmented_features=features)
        return dict(contract_version=VERSION, status='RESEARCH_PC_ENTRY_NOT_RELEASED',
            data_origin=data_origin, board_live=False, checkpoint_sha256=self.manifest['weights_sha256'],
            parameters=29441, sample_rate_hz=FS, window_samples=WINDOW,
            audio_interval_s=[float(audio_start_s),float(audio_end_s)],
            timestamp_domain='input_audio_clock; video synchronization unimplemented',
            contiguous_input=True, threshold=.65, NMS_deg=15., sources=sources,
            source_count=len(sources), ids_are_per_window_not_tracks=True, source_kind=None,
            scores_are_calibrated_probability=False, observed_directions=obs['directions'].tolist(),
            candidate_valid=obs['valid'].tolist(), candidate_logits=logits.tolist(),
            candidate_scores=scores.tolist(), heatmap=heatmap,
            timings_s=dict(physical_features=front_s, band_contrast=contrast_s,
                network=net_s, decode=decode_s, heatmap=heatmap_s,
                algorithm_total=front_s+contrast_s+net_s+decode_s,
                processing_with_heatmap=front_s+contrast_s+net_s+decode_s+heatmap_s),
            observation_wait_s=DURATION, continuous_realtime_certified=False)


class ContiguousPCStream:
    """One synchronous window buffer; gaps invalidate state until explicit restart.

    The caller must propagate packet-loss flags even if it compacts sample indices.
    No implicit source tracking, interpolation, padding or unbounded output queue.
    """
    def __init__(self, engine, quiet_reference, *, start_sample, start_time_s, data_origin):
        self.engine = engine
        self.quiet = normalized_pcm(quiet_reference, ((REFERENCE,16),(4,2048,16))).reshape(REFERENCE,16).copy()
        self.quiet.setflags(write=False)
        self.buffer = np.empty((WINDOW,16), np.float32)
        self.data_origin = data_origin
        self.completed_windows = 0
        self.discarded_samples = 0
        self.restart(start_sample=start_sample, start_time_s=start_time_s)

    def restart(self, *, start_sample, start_time_s):
        if type(start_sample) is not int or start_sample < 0 or not math.isfinite(start_time_s) or self.data_origin not in ORIGINS:
            raise ValueError('连续段须有明确样本序号、时间戳及来源')
        self.discarded_samples += getattr(self, 'pending', 0)
        self.pending = 0; self.origin_sample = self.expected_sample = start_sample
        self.origin_time = float(start_time_s)
        self.interrupted = self.closed = False
        self.latest_result = None

    def _interrupt(self, message):
        self.discarded_samples += self.pending
        self.pending = 0; self.latest_result = None; self.interrupted = True
        raise ValueError(message)

    def push(self, chunk, *, first_sample, audio_start_s, audio_end_s, sample_rate, contiguous):
        if self.closed or self.interrupted:
            raise RuntimeError('接收段已关闭或不连续；必须显式restart后再推理')
        if (sample_rate != FS or contiguous is not True or type(first_sample) is not int or first_sample != self.expected_sample):
            self._interrupt('采样率／丢包标记／重复或跳变样本序号错误，已清除残窗和旧结果')
        raw = np.asarray(chunk)
        if raw.ndim != 2 or raw.shape[1] != 16 or not 1 <= len(raw) <= WINDOW:
            self._interrupt('每块须为1到40960样本×16通道；实行一个窗的同步背压')
        if np.shares_memory(raw, self.quiet):
            self._interrupt('声窗不能复用静音参考')
        try:
            x = normalized_pcm(raw, (raw.shape,))
        except ValueError as e:
            self._interrupt(str(e))
        expected_time = self.origin_time+(first_sample-self.origin_sample)/FS
        if not all(math.isfinite(t) for t in (audio_start_s,audio_end_s)) or abs(audio_start_s-expected_time)>1e-6 or abs(audio_end_s-audio_start_s-len(x)/FS)>1e-6:
            self._interrupt('块时间戳不连续或与样本数不符，已清除残窗和旧结果')
        result = []; offset = 0
        while offset < len(x):
            take = min(WINDOW-self.pending, len(x)-offset)
            self.buffer[self.pending:self.pending+take] = x[offset:offset+take]
            self.pending += take; self.expected_sample += take; offset += take
            if self.pending == WINDOW:
                begin = self.expected_sample-WINDOW
                begin_t = self.origin_time+(begin-self.origin_sample)/FS
                try:
                    output = self.engine.infer_window(self.buffer, self.quiet, sample_rate=FS,
                        audio_start_s=begin_t, audio_end_s=begin_t+DURATION,
                        data_origin=self.data_origin, contiguous=True)
                except Exception:
                    self.discarded_samples += self.pending; self.pending = 0
                    self.latest_result = None; self.interrupted = True
                    raise
                output['stream_window'] = dict(index=self.completed_windows,
                    input_start_sample=begin, input_end_sample_exclusive=self.expected_sample,
                    synchronous_backpressure=True, input_buffer_bytes=self.buffer.nbytes,
                    quiet_reference_bytes=self.quiet.nbytes, maximum_queued_windows=1,
                    cross_window_source_tracking=False)
                self.completed_windows += 1; self.pending = 0
                self.latest_result = output; result.append(output)
        assert len(result) <= 1
        return result

    def finish(self, *, discard_tail=False):
        if self.closed or self.interrupted:
            raise RuntimeError('接收段已关闭或不连续')
        if self.pending and not discard_tail:
            raise ValueError('尾窗不完整，须继续提交或明确discard_tail=True，不能补零')
        tail = self.pending
        self.discarded_samples += tail; self.pending = 0; self.closed = True
        if tail: self.latest_result = None
        return dict(completed_windows=self.completed_windows, discarded_tail_samples=tail,
            total_discarded_samples=self.discarded_samples, zero_padding=False,
            maximum_queued_windows=1, input_buffer_bytes=self.buffer.nbytes)
