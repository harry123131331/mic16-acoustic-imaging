"""Equivalent PC observation operations; no weight or decoder changes."""
from __future__ import annotations
from types import FunctionType, SimpleNamespace
import numpy as np
from observed_runtime_fast import fast_inference_class, fast_local_vote_statistics
from mic16_config import Mic16Config
from config import SSLConfig

VERSION = 'mic16_equivalent_physics_runtime_v1_20261009'


def weight_metrics(curves,cfg=SSLConfig(),pair_count=32,valid_energy_floor=1e-6):
    """Only the four unchanged metrics consumed by default channel weights."""
    from gcc_phat_features import as_pair_lag
    x,squeeze = as_pair_lag(curves,cfg=cfg,pair_count=pair_count)
    flat = x.reshape(-1,x.shape[-1])
    peaks = np.argmax(flat,axis=1)
    peak = flat[np.arange(len(flat)),peaks]
    psr = np.empty(len(flat),np.float32); sharp = np.empty_like(psr)
    # 相同峰位对应相同旁瓣掩模和长度；保持旁瓣向量
    # 连续存储，以保留原float32归约顺序。
    for position in np.unique(peaks):
        indices = np.flatnonzero(peaks == position)
        mask = np.ones(flat.shape[1],bool)
        mask[max(0,int(position)-1):min(flat.shape[1],int(position)+2)] = False
        side = np.ascontiguousarray(flat[indices][:,mask])
        mean = side.mean(1).astype(float)
        std = np.sqrt(side.var(1)+1e-8).astype(float)
        psr[indices] = (peak[indices].astype(float)-mean)/(std+1e-6)
        sharp[indices] = peak[indices].astype(float)-side.max(1).astype(float)
    shape = x.shape[:2]
    result = dict(peak_abs=np.abs(peak).reshape(shape),
        peak_to_sidelobe_ratio=psr.reshape(shape),peak_sharpness=sharp.reshape(shape),
        pair_valid_flag=(np.sum(x*x,axis=2) > float(valid_energy_floor)).astype(np.float32))
    return {k:v[0] for k,v in result.items()} if squeeze else result


def batched_channel_weights(frames,gcc,quality,weight):
    curves = np.stack([gcc.extract_gcc_phat_features(frame,flatten=False) for frame in frames])
    original = quality.compute_pair_reliability
    namespace = dict(original.__globals__); namespace['pair_quality_metrics'] = weight_metrics
    function = FunctionType(original.__code__,namespace,original.__name__,original.__defaults__,original.__closure__)
    reliability,_ = function(curves)
    result = np.asarray([weight.pair_reliability_to_channel_weights(r) for r in reliability])
    assert result.shape == (20,16) and np.isfinite(result).all() and np.allclose(result.sum(1),1,atol=1e-6)
    return result


def equivalent_inference_class(base_class,feature_module):
    """Isolated function namespaces and a per-window spectrum cache."""
    fast = fast_inference_class(base_class,feature_module)
    original = base_class.infer
    feature = feature_module.extract_candidate_features
    tf = original.__globals__['tf']
    original_bands = feature.__globals__['beam_band_features']

    class EquivalentInference(fast):
        def infer(self,pcm,quiet_reference):
            original_spectrum = self.projector._frame_spectra
            cache = {}
            def spectrum(frame):
                a = np.asarray(frame)
                key = (a.__array_interface__['data'][0],a.shape,a.strides,a.dtype.str)
                if key not in cache:
                    value = original_spectrum(frame); value.setflags(write=False); cache[key] = value
                return cache[key]
            feature_ns = dict(feature.__globals__)
            feature_ns.update(local_vote_statistics=fast_local_vote_statistics)
            def band_features(beam,mask,frame_count,cfg=Mic16Config()):
                # 安静参考也可能有20帧，因此通过上次波束调用
                # 捕获的精确对象识别安静波束。
                if beam is self._quiet_candidate_beam:
                    values = np.asarray(beam,float).reshape(frame_count,cfg.frame_samples)
                    valid = np.asarray(mask,bool).reshape(frame_count,cfg.frame_samples)
                    level = 10*np.log10(np.maximum(np.mean(values**2,axis=1),1e-20))
                    return None,level,valid.mean(1)
                return original_bands(beam,mask,frame_count,cfg)
            def candidate_beam(values,direction,cfg=Mic16Config()):
                result = feature.__globals__['causal_candidate_beam'](values,direction,cfg)
                self._quiet_candidate_beam = result[0] if values is quiet_reference or np.shares_memory(values,quiet_reference) else None
                return result
            feature_ns.update(causal_candidate_beam=candidate_beam,beam_band_features=band_features)
            features = FunctionType(feature.__code__,feature_ns,feature.__name__,feature.__defaults__,feature.__closure__)
            observed = tf.observed_features
            observed_ns = dict(observed.__globals__)
            # 原特征函数对每个候选分别调用STFT和重采样；
            # 单批调用使用同样逐轴FIR与FFT算术，
            # 启用前须核验全部下游输出。
            def observed_features(frames,directions,reference,beam_waveforms):
                beams = np.asarray(beam_waveforms,float)
                batched = tf.stft(tf.resample(beams.T)).transpose(2,0,1)
                index = 0
                original_resample,original_stft = tf.resample,tf.stft
                # 只有候选调用使用捕获的波束对象，观测PCM
                # 与安静参考调用保留原函数。
                def resample(values):
                    nonlocal index
                    a = np.asarray(values)
                    if a.ndim == 1 and index < len(beams) and np.shares_memory(a,beams):
                        value = batched[index]; index += 1
                        return ('precomputed_candidate_spectrum',value)
                    return original_resample(values)
                def stft(values):
                    if isinstance(values,tuple) and values[0] == 'precomputed_candidate_spectrum':
                        return values[1]
                    return original_stft(values)
                namespace = dict(observed_ns); namespace.update(resample=resample,stft=stft)
                function = FunctionType(observed.__code__,namespace,observed.__name__,observed.__defaults__,observed.__closure__)
                return function(frames,directions,reference,beams)
            tf_view = SimpleNamespace(**vars(tf)); tf_view.observed_features = observed_features
            namespace = dict(original.__globals__)
            namespace.update(pc_wdas=self._cached_wdas,extract_candidate_features=features,tf=tf_view,
                channel_weights=batched_channel_weights)
            function = FunctionType(original.__code__,namespace,original.__name__,original.__defaults__,original.__closure__)
            self._window_cache = None; self._quiet_candidate_beam = None
            self.projector._frame_spectra = spectrum
            try:
                result = function(self,pcm,quiet_reference)
            finally:
                self.projector._frame_spectra = original_spectrum
                self._window_cache = None; self._quiet_candidate_beam = None
            result['equivalent_runtime_version'] = VERSION
            return result
    return EquivalentInference
