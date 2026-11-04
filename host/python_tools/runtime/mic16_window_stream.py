"""Bounded PCM chunk receiver for the existing full-window model.

This adapter preserves window inference. It does not join enhanced audio,
track speakers, overlap observations or shorten the model's observation time.
"""
from __future__ import annotations
from fractions import Fraction
import time
import numpy as np

VERSION = 'mic16_contiguous_pcm_window_receiver_v1_20261009'
SAMPLE_RATE = 39062.5
WINDOW_SAMPLES = 40960
CHANNELS = 16
OUTPUT_RATE = 16000


class Mic16WindowStream:
    """Synchronous inference with one fixed-size input buffer and backpressure.

The caller supplies consecutive chunks, in order. Transport packet sequence
checking belongs to the receiver; this class cannot detect missing packets.
"""
    def __init__(self, engine, quiet_reference, sample_rate=SAMPLE_RATE):
        if sample_rate != SAMPLE_RATE:
            raise ValueError('采样率必须精确为39062.5Hz')
        quiet = self._floating(quiet_reference)
        if quiet.ndim != 3 or quiet.shape[0] < 1 or quiet.shape[1:] != (2048, CHANNELS):
            raise ValueError('独立静音参考须为[T,2048,16]，T至少1')
        self.quiet_reference = quiet.copy()
        self.quiet_reference.setflags(write=False)
        self.engine = engine
        self.buffer = np.empty((WINDOW_SAMPLES, CHANNELS), np.float32)
        self.pending_samples = self.received_samples = self.completed_windows = 0
        self.closed = self.failed = False

    @staticmethod
    def _floating(values):
        data = np.asarray(values)
        if not np.issubdtype(data.dtype, np.floating):
            raise ValueError('PCM须先按实际满量程归一化为浮点幅值')
        if not np.isfinite(data).all():
            raise ValueError('PCM和静音参考须为有限值')
        with np.errstate(over='ignore'):
            converted = data.astype(np.float32, copy=False)
        if not np.isfinite(converted).all():
            raise ValueError('幅值超出FP32可表示范围')
        return converted

    def push(self, chunk):
        """Return each complete window immediately; never pad a partial window."""
        if self.closed or self.failed:
            raise RuntimeError('接收器已结束或推理失败，不能继续提交样本')
        samples = self._floating(chunk)
        if samples.ndim != 2 or samples.shape[1] != CHANNELS:
            raise ValueError('每个PCM块须为[样本数,16]')
        outputs, offset = [], 0
        while offset < len(samples):
            take = min(WINDOW_SAMPLES-self.pending_samples, len(samples)-offset)
            self.buffer[self.pending_samples:self.pending_samples+take] = samples[offset:offset+take]
            self.pending_samples += take; self.received_samples += take; offset += take
            if self.pending_samples == WINDOW_SAMPLES:
                started = time.perf_counter()
                try:
                    result = self.engine.infer(self.buffer, self.quiet_reference)
                except Exception:
                    self.failed = True
                    raise
                result = dict(result)
                begin = self.completed_windows*WINDOW_SAMPLES
                end = begin+WINDOW_SAMPLES
                # 局部重采样网格从全局16kHz时轴上的精确
                # 有理数位置开始；局部长度四舍五入后
                # 不能直接拼接成连续波形。
                origin = Fraction(begin*256, 625)
                result['stream_window'] = dict(index=self.completed_windows,
                    input_start_sample=begin, input_end_sample_exclusive=end,
                    input_start_seconds=begin/SAMPLE_RATE, input_end_seconds=end/SAMPLE_RATE,
                    output_grid_origin_numerator=origin.numerator,
                    output_grid_origin_denominator=origin.denominator,
                    receiver_inference_ms=1000*(time.perf_counter()-started),
                    chunk_receiver_version=VERSION, window_inference=True,
                    continuous_audio_joined=False, cross_window_source_tracking=False)
                outputs.append(result)
                self.completed_windows += 1; self.pending_samples = 0
        return outputs

    def finish(self, discard_tail=False):
        if self.closed or self.failed:
            raise RuntimeError('接收器已结束或推理失败')
        if self.pending_samples and not discard_tail:
            raise ValueError(f'尚有{self.pending_samples}样本不足40960；继续提交或明确丢弃尾窗')
        tail = self.pending_samples
        self.closed = True
        return dict(received_samples=self.received_samples, completed_windows=self.completed_windows,
            processed_samples=self.completed_windows*WINDOW_SAMPLES, discarded_tail_samples=tail,
            zero_padding=False, observation_seconds=WINDOW_SAMPLES/SAMPLE_RATE,
            input_buffer_bytes=self.buffer.nbytes, synchronous_backpressure=True,
            live_transport_tested=False, continuous_audio_joined=False,
            cross_window_source_tracking=False)
