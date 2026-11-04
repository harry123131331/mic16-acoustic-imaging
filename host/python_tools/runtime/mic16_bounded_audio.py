"""Bounded synchronous PCM inference and globally aligned audio packets.

One observed window of lookahead preserves the offline boundary arithmetic.
No model, source count, identity or truth changes. Sink callbacks must consume
packets synchronously; an unbounded downstream queue is outside this API.
"""
from __future__ import annotations
from collections import deque
import time
import numpy as np
from scipy.signal import resample_poly
from mic16_window_stream import Mic16WindowStream
from mic16_continuous_audio import (SpatialTracks,steered_segment,interpolate_mask,ramp,
    N,FS,UP,DOWN,RATE,FADE_SAMPLES,HISTORY_SAMPLES)
from research import directional_tf_mask as tf

VERSION = 'mic16_bounded_global_audio_one_window_lookahead_v1_20261009'
CONTEXT = 2048


class BoundedSpatialTracks(SpatialTracks):
    def update(self,directions,scores):
        # 匹配前移除退役状态，保留仍有效轨迹的ID与
        # 顺序，最多保留三组各8个方向。
        self.tracks = {i:t for i,t in self.tracks.items()
            if self.next_window-t['last_window'] <= self.max_missed+1}
        return super().update(directions,scores)


def snapshot_prediction(directions,ids,masks,weights):
    d = np.asarray(directions,float).reshape(-1,2).copy()
    ids = list(ids); m = np.asarray(masks,float).copy(); w = np.asarray(weights,float).copy()
    if len(ids) > 8 or len(set(ids)) != len(ids) or any(not isinstance(i,(int,np.integer)) or isinstance(i,bool) or i < 1 for i in ids):
        raise ValueError('最多8个唯一正整数匿名编号必需')
    if d.shape != (len(ids),2) or not np.isfinite(d).all() or np.any(np.abs(d) >= 90):
        raise ValueError('有限前半球方向与编号须一致')
    if m.shape != (len(ids),67,64) or not np.isfinite(m).all() or np.any((m < .8-1e-6)|(m > 1.2+1e-6)):
        raise ValueError('真实67乘64预测掩码须位于0.8到1.2')
    if ids and (w.shape != (20,16) or not np.isfinite(w).all() or np.any(w < 0) or not np.allclose(w.sum(1),1.,atol=1e-6)):
        raise ValueError('20乘16归一观测通道权重必需')
    if not ids:
        w = np.empty((0,16))
    return dict(directions_deg=d,track_ids=ids,masks=m,observed_channel_weights=w)


class BoundedAudio:
    """Retain at most3 full PCM windows and their compact predictions."""
    def __init__(self):
        self.blocks = deque(); self.windows = 0; self.emitted = 0; self.closed = False
        self.max_pcm_bytes = self.max_prediction_bytes = self.max_blocks = 0

    def _stats(self):
        pcm = sum(b['pcm'].nbytes for b in self.blocks)
        predictions = sum(sum(b['prediction'][k].nbytes for k in
            ('directions_deg','masks','observed_channel_weights')) for b in self.blocks)
        self.max_pcm_bytes = max(self.max_pcm_bytes,pcm)
        self.max_prediction_bytes = max(self.max_prediction_bytes,predictions)
        self.max_blocks = max(self.max_blocks,len(self.blocks))
        assert len(self.blocks) <= 3

    def accept(self,pcm,prediction):
        if self.closed:
            raise RuntimeError('合成器已结束')
        values = np.asarray(pcm)
        if values.shape != (N,16):
            raise ValueError('每次提交一个完整PCM窗')
        x = Mic16WindowStream._floating(values)
        p = snapshot_prediction(prediction['directions_deg'],prediction['track_ids'],
            prediction['masks'],prediction['observed_channel_weights'])
        self.blocks.append(dict(index=self.windows,pcm=x.copy(),prediction=p))
        self.windows += 1; self._stats()
        if self.windows < 2:
            return None
        packet = self._emit(self.windows-2,final=False)
        while len(self.blocks) > 2:
            self.blocks.popleft()
        return packet

    def finish(self):
        if self.closed:
            raise RuntimeError('合成器已结束')
        packet = self._emit(self.windows-1,final=True) if self.windows else None
        self.closed = True; self.blocks.clear()
        return packet

    def _emit(self,index,final):
        assert index == self.emitted
        records = {b['index']:b['prediction'] for b in self.blocks}
        pcm_origin = self.blocks[0]['index']*N
        pcm = np.concatenate([b['pcm'] for b in self.blocks])
        target_start,target_end = index*N,(index+1)*N
        origin = max(0,(target_start-CONTEXT)//DOWN*DOWN)
        end = min(self.windows*N,target_end+CONTEXT)
        assert origin >= pcm_origin and (origin == 0 or origin-pcm_origin >= HISTORY_SAMPLES)
        begin_output = (target_start*UP+DOWN-1)//DOWN
        end_output = (target_end*UP+DOWN-1)//DOWN
        offset = origin//DOWN*UP
        selection = slice(begin_output-offset,end_output-offset)
        ids = sorted({i for p in records.values() for i in p['track_ids']})
        assert len(ids) <= 24
        tracks = {}
        fade = ramp()
        def steered(start,stop,direction,weights):
            return steered_segment(pcm,start-pcm_origin,stop-pcm_origin,direction,weights)
        for track in ids:
            raw = np.zeros(end-origin,float)
            for window in range(origin//N,(end-1)//N+1):
                start,stop = max(origin,window*N),min(end,(window+1)*N)
                current,old = records.get(window),records.get(window-1)
                current_index = current['track_ids'].index(track) if current and track in current['track_ids'] else None
                old_index = old['track_ids'].index(track) if old and track in old['track_ids'] else None
                local_start,local_end = start-window*N,stop-window*N
                region = slice(start-origin,stop-origin)
                if current_index is not None:
                    direction = current['directions_deg'][current_index]
                    weights = np.repeat(current['observed_channel_weights'],2048,axis=0)[local_start:local_end]
                    beam = steered(start,stop,direction,weights)
                    cross_end = min(local_end,FADE_SAMPLES)
                    if window and local_start < cross_end:
                        count = cross_end-local_start; fraction = fade[local_start:cross_end]
                        if old_index is not None:
                            continuation = steered(start,start+count,old['directions_deg'][old_index],old['observed_channel_weights'][-1])
                            beam[:count] = (1-fraction)*continuation+fraction*beam[:count]
                        else:
                            beam[:count] *= fraction
                    raw[region] = beam
                elif old_index is not None and local_start < FADE_SAMPLES:
                    count = min(local_end,FADE_SAMPLES)-local_start
                    raw[start-origin:start-origin+count] = (1-fade[local_start:local_start+count])*steered(
                        start,start+count,old['directions_deg'][old_index],old['observed_channel_weights'][-1])
            baseline = resample_poly(raw,UP,DOWN)
            spectrum = tf.stft(baseline)
            masks = np.ones((len(spectrum),64),float)
            for local_frame in range(len(spectrum)):
                center = (origin//DOWN+local_frame)*DOWN
                window = center//N
                if final:
                    window = min(window,self.windows-1)
                local = center-window*N
                current,old = records.get(window),records.get(window-1)
                current_mask = current['masks'][current['track_ids'].index(track)] if current and track in current['track_ids'] else None
                old_mask = old['masks'][old['track_ids'].index(track)] if old and track in old['track_ids'] else None
                if current_mask is not None:
                    value = interpolate_mask(current_mask,local/DOWN)
                    if old_mask is not None and 0 <= local < FADE_SAMPLES:
                        mix = .5-.5*np.cos(np.pi*local/(FADE_SAMPLES-1))
                        value = (1-mix)*interpolate_mask(old_mask,(N+local)/DOWN)+mix*value
                    masks[local_frame] = value
                elif old_mask is not None and 0 <= local < FADE_SAMPLES:
                    masks[local_frame] = interpolate_mask(old_mask,(N+local)/DOWN)
            full = np.concatenate((masks[:,:1],np.repeat(masks,4,axis=1)),axis=1)
            enhanced = tf.istft(spectrum*full,len(baseline))
            tracks[track] = dict(WDAS=baseline[selection].copy(),enhanced=enhanced[selection].copy())
        input_block = pcm[origin-pcm_origin:end-pcm_origin,0].astype(float)
        input_mic0 = resample_poly(input_block,UP,DOWN)[selection].copy()
        assert len(input_mic0) == end_output-begin_output
        self.emitted += 1
        return dict(window=index,output_start_sample=begin_output,output_end_sample_exclusive=end_output,
            output_rate=RATE,input_start_sample=target_start,input_end_sample_exclusive=target_end,
            available_input_samples=self.windows*N,lookahead_windows=0 if final else 1,final=final,
            input_MIC0=input_mic0,tracks=tracks,
            speaker_identity=False,audio_halo_is_not_a_detection=True)

    def state(self):
        return dict(retained_PCM_windows=len(self.blocks),maximum_retained_PCM_windows=self.max_blocks,
            maximum_retained_PCM_bytes=self.max_pcm_bytes,maximum_retained_prediction_array_bytes=self.max_prediction_bytes,
            received_windows=self.windows,emitted_audio_windows=self.emitted,
            one_observed_window_lookahead=True,output_sink_must_consume_synchronously=True)


class Mic16BoundedPipeline:
    """Serial backpressure, fixed input/audio history and synchronous callbacks."""
    def __init__(self,engine,quiet_reference,sample_rate=FS):
        self.receiver = Mic16WindowStream(engine,quiet_reference,sample_rate)
        self.tracker = BoundedSpatialTracks(gate_deg=10.,max_missed_windows=1)
        self.audio = BoundedAudio(); self.closed = self.failed = False
        self.max_track_states = 0

    def push(self,chunk,audio_sink,prediction_sink=None):
        if self.closed or self.failed:
            raise RuntimeError('流水线已结束或失败')
        data = np.asarray(chunk)
        if data.ndim != 2 or data.shape[1] != 16 or len(data) > N:
            raise ValueError('单次PCM块须为[0到40960,16]，更长输入须由调用方分块')
        samples = Mic16WindowStream._floating(data)
        offset = 0; completed = 0
        while offset < len(samples):
            take = min(N-self.receiver.pending_samples,len(samples)-offset)
            started = time.perf_counter()
            try:
                results = self.receiver.push(samples[offset:offset+take])
                offset += take
                if not results:
                    continue
                assert len(results) == 1
                result = results[0]; index = result['stream_window']['index']
                assignment = self.tracker.update(result['directions_deg'],result['output_scores'])
                self.max_track_states = max(self.max_track_states,len(self.tracker.tracks))
                assert len(self.tracker.tracks) <= 24
                prediction = dict(directions_deg=result['directions_deg'],track_ids=assignment['track_ids'],
                    masks=result['masks'],observed_channel_weights=result['observed_channel_weights'])
                packet = self.audio.accept(self.receiver.buffer,prediction)
                row = dict(window=index,directions_deg=result['directions_deg'],selected_candidates=result['selected_candidates'],
                    output_scores=result['output_scores'],association=assignment,inference_ms=result['compute_ms'],
                    inference_and_audio_compute_ms=1000*(time.perf_counter()-started))
                if prediction_sink is not None:
                    prediction_sink(row)
                if packet is not None:
                    audio_sink(packet)
                completed += 1
            except Exception:
                self.failed = True
                raise
        return dict(accepted_chunk_samples=len(samples),completed_windows=completed)

    def finish(self,audio_sink,discard_tail=False):
        if self.closed or self.failed:
            raise RuntimeError('流水线已结束或失败')
        received = self.receiver.finish(discard_tail=discard_tail)
        try:
            packet = self.audio.finish()
            if packet is not None:
                audio_sink(packet)
            self.closed = True
        except Exception:
            self.failed = True
            raise
        return dict(version=VERSION,received=received,audio_state=self.audio.state(),
            maximum_retained_track_states=self.max_track_states,
            receiver_buffer_bytes=self.receiver.buffer.nbytes,quiet_reference_bytes=self.receiver.quiet_reference.nbytes,
            retained_caches_bounded_by_window_count=True,full_recording_retained=False,
            live_transport_tested=False,PC_frozen=False,FPGA_frozen=False,new_training_runs=0)
