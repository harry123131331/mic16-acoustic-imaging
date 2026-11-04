"""Remove repeated observed-array work; leave models and decoding unchanged."""
from __future__ import annotations
from types import FunctionType
import numpy as np

VERSION = 'mic16_observation_reuse_vector_vote_v1_20261009'


def fast_local_vote_statistics(votes, direction):
    from acoustic_camera.valid_source_detection import angular_distance_deg
    a = np.asarray(votes, float)
    if a.shape != (480,9) or not np.isfinite(a).all():
        raise ValueError('votes must be finite [480,9]')
    if set(a[:,0]) != set(range(20)) or set(a[:,1]) != set(range(24)) or not np.all(np.isin(a[:,2],[0,1,2])):
        raise ValueError('invalid frame/zone/coarse-band indices')
    if len(set(map(tuple,a[:,:2]))) != 480:
        raise ValueError('duplicate frame-zone observations')
    az_a, el_a = np.deg2rad(np.asarray(direction,float))
    az_b, el_b = np.deg2rad(a[:,4]), np.deg2rad(a[:,5])
    cosine = np.sin(el_a)*np.sin(el_b)+np.cos(el_a)*np.cos(el_b)*np.cos(az_a-az_b)
    errors = np.rad2deg(np.arccos(np.clip(cosine,-1.,1.)))
    # 在对舍入敏感的边界保留原标量分支。
    for i in np.flatnonzero(np.abs(errors-10.) < 1e-9):
        errors[i] = angular_distance_deg(*direction,*a[i,4:6])
    near = errors <= 10.
    result = np.zeros((20,6),float)
    prefix_frames, prefix_bands = set(),set()
    for t in range(20):
        ids = np.flatnonzero(near & (a[:,0] == t))
        if len(ids):
            prefix_frames.add(t); prefix_bands.update(a[ids,2])
            result[t,3] = np.maximum(a[ids,7],0).mean()
            result[t,4] = np.clip(a[ids,8],0,1).mean()
            result[t,5] = errors[ids].mean()/10.
        result[t,0] = len(ids)/24.
        result[t,1] = len(prefix_frames)/(t+1)
        result[t,2] = len(prefix_bands)/3.
    return result


def fast_inference_class(base_class, feature_module):
    """Create an isolated subclass; never modify base function globals."""
    original_infer = base_class.infer
    original_features = feature_module.extract_candidate_features
    feature_globals = dict(original_features.__globals__)
    feature_globals['local_vote_statistics'] = fast_local_vote_statistics
    fast_features = FunctionType(original_features.__code__, feature_globals,
        original_features.__name__, original_features.__defaults__, original_features.__closure__)
    original_wdas = original_infer.__globals__['pc_wdas']
    physics = original_wdas.__globals__

    class FastMic16Inference(base_class):
        def _cached_wdas(self,pcm,direction,weights,weight_reference):
            if self._window_cache is None:
                self._window_cache = (pcm,weights,np.asarray(pcm,np.float64).reshape(40960,16),
                    np.repeat(weights,2048,axis=0))
            source_pcm, source_weights, x, expanded = self._window_cache
            if pcm is not source_pcm or weights is not source_weights:
                raise RuntimeError('同窗WDAS缓存不能用于另一份PCM或权重')
            cfg = physics['Mic16Config']()
            arrivals = physics['far_field_arrival_delays_samples_azel'](*direction,cfg).astype(float)
            common = int(np.ceil(cfg.array_radius_m*cfg.sample_rate/cfg.sound_speed))
            delay = common-arrivals; integer = np.floor(delay).astype(int)
            aligned = np.zeros_like(x)
            for channel in range(16):
                coefficient = weight_reference.lagrange5_coefficients(float(delay[channel]-integer[channel]))
                filtered = physics['lfilter'](coefficient,[1.],x[:,channel])
                d = int(integer[channel]); aligned[d:,channel] = filtered[:40960-d]
            beam = np.sum(aligned*expanded,axis=1)
            return beam,dict(common_delay_samples=common,guard_samples=int(integer.max())+5,
                far_field_azel=True,coefficients='frozen six-tap Lagrange5',precision='PC float, not FPGA fixed-point')

        def infer(self,pcm,quiet_reference):
            # 每个实例/窗拥有独立全局空间和缓存；
            # 与既有分块接收器一样是同步引擎。
            namespace = dict(original_infer.__globals__)
            namespace.update(pc_wdas=self._cached_wdas,extract_candidate_features=fast_features)
            function = FunctionType(original_infer.__code__,namespace,
                original_infer.__name__,original_infer.__defaults__,original_infer.__closure__)
            self._window_cache = None
            try:
                result = function(self,pcm,quiet_reference)
            finally:
                self._window_cache = None
            result['observation_runtime_version'] = VERSION
            return result
    return FastMic16Inference
