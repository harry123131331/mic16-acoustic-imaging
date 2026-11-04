"""Offline continuous reconstruction on one global sample/STFT grid.

The neural model keeps its existing40960-sample observation windows. Track
IDs describe observed spatial continuity, without speaker identity or truth.
"""
from __future__ import annotations
import numpy as np
from scipy.optimize import linear_sum_assignment
from scipy.signal import lfilter, resample_poly
from mic16_config import Mic16Config
from acoustic_camera.geometry_2d import far_field_arrival_delays_samples_azel
from research import directional_tf_mask as tf
from speech_enhancement.beamforming_convention import lagrange5_coefficients

VERSION = 'mic16_offline_global_grid_spatial_tracks_v1_20261009'
N, FS, UP, DOWN, RATE = 40960, 39062.5, 256, 625, 16000
FADE_SAMPLES, HISTORY_SAMPLES = 625, 32


def spherical_distances(a,b):
    a = np.deg2rad(np.asarray(a,float).reshape(-1,2))
    b = np.deg2rad(np.asarray(b,float).reshape(-1,2))
    cosine = np.sin(a[:,1,None])*np.sin(b[None,:,1])+np.cos(a[:,1,None])*np.cos(b[None,:,1])*np.cos(a[:,0,None]-b[None,:,0])
    return np.rad2deg(np.arccos(np.clip(cosine,-1.,1.)))


class SpatialTracks:
    def __init__(self,gate_deg=10.,max_missed_windows=1):
        if not 0 < gate_deg <= 30 or not isinstance(max_missed_windows,int) or max_missed_windows < 0:
            raise ValueError('合理的角度门限和非负整数漏窗数必需')
        self.gate_deg, self.max_missed = gate_deg,max_missed_windows
        self.tracks = {}; self.next_id = 1; self.next_window = 0

    def update(self,directions,scores):
        points = np.asarray(directions,float).reshape(-1,2)
        scores = np.asarray(scores,float)
        if len(points) > 8 or scores.shape != (len(points),) or not np.isfinite(points).all() or not np.isfinite(scores).all() or np.any(np.abs(points) >= 90) or np.any((scores < 0)|(scores > 1)):
            raise ValueError('最多8个有限前半球方向及0到1分数必需')
        window = self.next_window
        live = sorted(i for i,t in self.tracks.items() if window-t['last_window'] <= self.max_missed+1)
        distance = spherical_distances([self.tracks[i]['direction'] for i in live],points)
        n,k = len(live),len(points)
        cost = np.full((n+k,n+k),10000.)
        cost[:n,:k] = np.where(distance <= self.gate_deg,distance,10000.)
        cost[:n,k:] = 1000.; cost[n:,:k] = 1000.; cost[n:,k:] = 0.
        rows,columns = linear_sum_assignment(cost)
        mapping, matched = [None]*k,[]
        for i,j in zip(rows,columns):
            if i < n and j < k and distance[i,j] <= self.gate_deg:
                mapping[j] = live[i]
                matched.append(dict(track_id=live[i],output=int(j),error_deg=float(distance[i,j]),
                    missed_windows=window-self.tracks[live[i]]['last_window']-1))
        born = []
        for j,point in enumerate(points):
            if mapping[j] is None:
                mapping[j] = self.next_id; self.next_id += 1
                born.append(dict(track_id=mapping[j],output=j))
            self.tracks[mapping[j]] = dict(direction=point.tolist(),last_window=window,last_score=float(scores[j]))
        absent = [i for i in live if i not in mapping]
        self.next_window += 1
        return dict(window=window,track_ids=mapping,matched=matched,born=born,absent=absent,
            gate_deg=self.gate_deg,max_missed_windows=self.max_missed,
            interpretation='Anonymous spatial continuity only; no speaker identity or crossing-path identity guarantee')


def capturing_inference_class(base_class,feature_module,fast_factory):
    fast = fast_factory(base_class,feature_module)
    class CapturingInference(fast):
        def _cached_wdas(self,pcm,direction,weights,weight_reference):
            if self._audio_observed_weights is None:
                self._audio_observed_weights = weights.copy()
            return super()._cached_wdas(pcm,direction,weights,weight_reference)
        def infer(self,pcm,quiet_reference):
            self._audio_observed_weights = None
            result = super().infer(pcm,quiet_reference)
            result['observed_channel_weights'] = self._audio_observed_weights if self._audio_observed_weights is not None else np.empty((0,16))
            self._audio_observed_weights = None
            return result
    return CapturingInference


def steered_segment(pcm,start,end,direction,weights):
    """Use real preceding PCM for the causal FIR, with the same coefficients."""
    x = np.asarray(pcm,float)
    cfg = Mic16Config()
    arrivals = far_field_arrival_delays_samples_azel(*direction,cfg).astype(float)
    delay = int(np.ceil(cfg.array_radius_m*cfg.sample_rate/cfg.sound_speed))-arrivals
    integer = np.floor(delay).astype(int)
    if integer.min() < 0 or integer.max()+5 >= HISTORY_SAMPLES:
        raise ValueError('几何超出当前32样本历史支持')
    prefix = max(0,start-HISTORY_SAMPLES)
    block = x[prefix:end]
    positions = np.arange(start-prefix,end-prefix)
    aligned = np.zeros((end-start,16),float)
    for c in range(16):
        filtered = lfilter(lagrange5_coefficients(float(delay[c]-integer[c])),[1.],block[:,c])
        ids = positions-integer[c]; valid = ids >= 0
        aligned[valid,c] = filtered[ids[valid]]
    w = np.asarray(weights,float)
    if w.shape == (16,):
        w = np.broadcast_to(w,aligned.shape)
    if w.shape != aligned.shape or not np.isfinite(w).all():
        raise ValueError('逐样本16路观测权重必需')
    return np.sum(aligned*w,axis=1)


def ramp(samples=FADE_SAMPLES):
    return .5-.5*np.cos(np.linspace(0.,np.pi,samples))


def raw_track_beams(pcm,windows):
    x = np.asarray(pcm,float)
    if x.ndim != 2 or x.shape[1] != 16 or len(x) != len(windows)*N or not np.isfinite(x).all():
        raise ValueError('PCM与完整40960样本窗记录须一致')
    tracks = sorted({int(i) for w in windows for i in w['track_ids']})
    beams = {i:np.zeros(len(x),float) for i in tracks}
    previous = {}; transition_events = []
    fade = ramp()
    for index,window in enumerate(windows):
        start,end = index*N,(index+1)*N
        directions = np.asarray(window['directions_deg'],float).reshape(-1,2)
        ids = window['track_ids']; masks = np.asarray(window['masks'],float)
        if len(set(ids)) != len(ids) or len(directions) != len(ids) or masks.shape != (len(ids),67,64) or not np.isfinite(masks).all() or np.any((masks < .8-1e-6)|(masks > 1.2+1e-6)):
            raise ValueError('逐窗方向、唯一轨迹编号及真实67乘64掩码须一致')
        current = {}
        if ids:
            weights = np.asarray(window['observed_channel_weights'],float)
            if weights.shape != (20,16) or not np.isfinite(weights).all() or np.any(weights < 0) or not np.allclose(weights.sum(1),1.,atol=1e-6):
                raise ValueError('观测通道权重须为20乘16归一非负数')
            repeated = np.repeat(weights,2048,axis=0)
            for j,track in enumerate(ids):
                beam = steered_segment(x,start,end,directions[j],repeated)
                if track in previous:
                    old = previous[track]
                    continuation = steered_segment(x,start,start+FADE_SAMPLES,old['direction'],old['last_weights'])
                    beam[:FADE_SAMPLES] = (1-fade)*continuation+fade*beam[:FADE_SAMPLES]
                    kind = 'observed_direction_weight_crossfade'
                elif index:
                    beam[:FADE_SAMPLES] *= fade
                    kind = 'birth_or_reappearance_fade_in'
                else:
                    kind = 'initial_window'
                beams[track][start:end] = beam
                current[track] = dict(direction=directions[j].tolist(),last_weights=weights[-1].copy())
                transition_events.append(dict(window=index,track_id=int(track),kind=kind,fade_samples=FADE_SAMPLES if kind != 'initial_window' else 0))
        for track,old in previous.items():
            if track not in current:
                beams[track][start:start+FADE_SAMPLES] = (1-fade)*steered_segment(x,start,start+FADE_SAMPLES,old['direction'],old['last_weights'])
                transition_events.append(dict(window=index,track_id=int(track),kind='not_detected_fade_out',fade_samples=FADE_SAMPLES))
        previous = current
    return beams,transition_events


def interpolate_mask(mask,frame_coordinate):
    position = float(np.clip(frame_coordinate,0,len(mask)-1))
    a = int(np.floor(position)); b = min(a+1,len(mask)-1); f = position-a
    return (1-f)*mask[a]+f*mask[b]


def global_track_masks(windows,track,frame_count):
    result = np.ones((frame_count,64),float)
    predictions = {}
    for index,window in enumerate(windows):
        if track in window['track_ids']:
            predictions[index] = np.asarray(window['masks'][window['track_ids'].index(track)],float)
    for j in range(frame_count):
        # 全局16kHz的256点步长精确对应原PCM的625点。
        index = min(j*625//N,len(windows)-1)
        local = j*625-index*N
        old = predictions.get(index-1); current = predictions.get(index)
        if current is not None:
            value = interpolate_mask(current,local/625.)
            if old is not None and 0 <= local < FADE_SAMPLES:
                mix = .5-.5*np.cos(np.pi*local/(FADE_SAMPLES-1))
                value = (1-mix)*interpolate_mask(old,(N+local)/625.)+mix*value
            result[j] = value
        elif old is not None and 0 <= local < FADE_SAMPLES:
            result[j] = interpolate_mask(old,(N+local)/625.)
    return result


def reconstruct(pcm,windows):
    """Return full-duration anonymous tracks; no truth or component inputs."""
    beams,events = raw_track_beams(pcm,windows)
    samples = (len(pcm)*UP+DOWN-1)//DOWN
    outputs = {}
    for track,beam in beams.items():
        baseline = resample_poly(beam,UP,DOWN)
        assert len(baseline) == samples
        spectrum = tf.stft(baseline)
        mask = global_track_masks(windows,track,len(spectrum))
        full = np.concatenate((mask[:,:1],np.repeat(mask,4,axis=1)),axis=1)
        enhanced = tf.istft(spectrum*full,samples)
        outputs[track] = dict(raw_beam=beam,WDAS=baseline,enhanced=enhanced,global_mask=mask)
    input_mic0 = resample_poly(np.asarray(pcm,float)[:,0],UP,DOWN)
    return dict(tracks=outputs,input_MIC0=input_mic0,output_samples=samples,transition_events=events,
        metadata=dict(version=VERSION,global_resampling=True,global_STFT_OLA=True,
            track_ids_are_anonymous_spatial_observations=True,fade_original_samples=FADE_SAMPLES,
            FIR_history_samples=HISTORY_SAMPLES,offline_file_reconstruction=True,
            live_transport_tested=False,realtime_continuous_throughput_passed=False,new_training_runs=0))
