"""Bounded input worker; inference and all-source writing have one owner.

The model must be initialized before starting this worker (legacy import wiring
is startup-only). Latest display metadata may coalesce; accepted PCM may not.
"""
from collections import deque
import threading
import time

import numpy as np


class ContinuousInputWorker:
    def __init__(self, pipeline, writer, *, max_items=8, max_bytes=2*1024*1024,
                 prediction_sink=None):
        if type(max_items) is not int or not 1 <= max_items <= 64 or not 1024 <= max_bytes <= 16*1024*1024:
            raise ValueError('Invalid input queue budget')
        self.pipeline, self.writer = pipeline, writer
        self.max_items, self.max_bytes = max_items, max_bytes
        self.prediction_sink = prediction_sink
        self._condition = threading.Condition()
        self._queue = deque()
        self._thread = None
        self._input_closed = self._cancelled = False
        self._error = ''
        self._result = None
        self._latest = None
        self._stats = dict(accepted_chunks=0, accepted_samples=0, processed_chunks=0, processed_samples=0,
            queued_bytes=0, queued_items=0, peak_queued_bytes=0, peak_queued_items=0,
            maximum_inflight_chunk_bytes=0, completed_windows=0, emitted_packets=0,
            output_samples=0, progress_revision=0, rejected_chunks=0)

    def start(self):
        with self._condition:
            if self._thread is not None:
                raise RuntimeError('Worker may start once')
            self._thread = threading.Thread(target=self._run, name='mic16-all-source-writer', daemon=True)
            self._thread.start()
        return self

    @property
    def result(self):
        with self._condition:
            return self._result

    def snapshot(self):
        with self._condition:
            return dict(self._stats), self._latest

    def _reject(self, message):
        self._stats['rejected_chunks'] += 1
        self._error = self._error or message
        self._input_closed = True
        self._condition.notify_all()
        return False

    def submit(self, pcm, *, first_sample, audio_start_s, audio_end_s, contiguous,
               sample_rate=39062.5, block=False, timeout=None):
        """File readers use block=True. Nonblocking overflow fails the segment.

        Live callers must never keep submitting after False; there is no drop
        oldest or drop latest PCM policy. Rejected data is not silently padded.
        """
        x = np.asarray(pcm)
        with self._condition:
            if self._thread is None:
                raise RuntimeError('Start worker before submitting')
            if self._input_closed or self._result is not None:
                return False
            if (x.dtype != np.float32 or x.ndim != 2 or x.shape[1] != 16 or not 1 <= len(x) <= 4096
                    or not np.isfinite(x).all() or np.max(np.abs(x)) > 1):
                return self._reject('INPUT_FORMAT: explicit normalized float32 PCM required')
            if (contiguous is not True or type(first_sample) is not int or
                    first_sample != self._stats['accepted_samples'] or sample_rate != 39062.5 or
                    not all(np.isfinite(t) for t in (audio_start_s, audio_end_s)) or
                    abs(audio_start_s-self.pipeline.start_time-first_sample/sample_rate) > 1e-6 or
                    abs(audio_end_s-audio_start_s-len(x)/sample_rate) > 1e-6):
                return self._reject('INPUT_TIMELINE: gap, duplicate, rate or timestamp mismatch')
            size = x.size * 4
            if size > self.max_bytes:
                return self._reject('INPUT_BUDGET: chunk exceeds queue budget')
            deadline = None if timeout is None else time.monotonic()+timeout
            while len(self._queue) >= self.max_items or self._stats['queued_bytes']+size > self.max_bytes:
                if not block:
                    return self._reject('INPUT_QUEUE_OVERFLOW: session incomplete; no PCM silently dropped')
                remaining = None if deadline is None else deadline-time.monotonic()
                if remaining is not None and remaining <= 0:
                    return self._reject('INPUT_QUEUE_TIMEOUT: session incomplete')
                self._condition.wait(remaining)
                if self._input_closed or self._result is not None:
                    return False
                # 一个片段只能由一个有序生产者输入。
                if first_sample != self._stats['accepted_samples']:
                    return self._reject('MULTIPLE_PRODUCERS: input order changed while waiting')
            owned = np.array(x, dtype=np.float32, order='C', copy=True)
            owned.setflags(write=False)
            metadata = dict(first_sample=first_sample, audio_start_s=audio_start_s,
                audio_end_s=audio_end_s, contiguous=True, sample_rate=sample_rate)
            self._queue.append((owned, metadata))
            self._stats['accepted_chunks'] += 1
            self._stats['accepted_samples'] += len(owned)
            self._stats['queued_bytes'] += size
            self._stats['queued_items'] += 1
            for unit in ('bytes', 'items'):
                self._stats['peak_queued_'+unit] = max(self._stats['peak_queued_'+unit], self._stats['queued_'+unit])
            self._condition.notify_all()
            return True

    def finish_input(self):
        """Close input, drain accepted PCM and flush. Nonblocking for UI callers."""
        with self._condition:
            self._input_closed = True
            self._condition.notify_all()

    def cancel(self):
        """Cooperative stop after an in-flight call; no successful completion claim."""
        with self._condition:
            if self._result is None:
                self._cancelled = self._input_closed = True
                self._condition.notify_all()

    def wait(self, timeout=None):
        if self._thread is None:
            raise RuntimeError('Worker not started')
        self._thread.join(timeout)
        return not self._thread.is_alive()

    def _prediction(self, index, result, prediction):
        tracks = [dict(source_id=int(t['source_id']), direction=list(map(float, t['direction'])),
                       score=float(t['score'])) for t in result['detection']['tracks']
                  if t['confirmed_this_cycle']]
        with self._condition:
            self._stats['completed_windows'] += 1
            self._stats['progress_revision'] += 1
            self._latest = dict(cycle=index+1, current_confirmed=tracks,
                segment_id=self.pipeline.segment_id, pose_key=self.pipeline.pose_key,
                input_origin=self.pipeline.data_origin, stats=dict(self._stats))
        if self.prediction_sink is not None:
            self.prediction_sink(index, result, prediction)

    def _audio(self, packet):
        self.writer.accept(packet)
        with self._condition:
            self._stats['emitted_packets'] += 1
            self._stats['output_samples'] = packet['output_end_sample_exclusive']

    def _run(self):
        pipeline_state = session = None
        try:
            while True:
                with self._condition:
                    while not self._queue and not self._input_closed:
                        self._condition.wait()
                    if self._error or self._cancelled:
                        break
                    if not self._queue:
                        break
                    pcm, metadata = self._queue.popleft()
                    self._stats['queued_items'] -= 1
                    self._stats['queued_bytes'] -= pcm.nbytes
                    self._stats['maximum_inflight_chunk_bytes'] = max(self._stats['maximum_inflight_chunk_bytes'], pcm.nbytes)
                    self._condition.notify_all()
                self.pipeline.push(pcm, **metadata, audio_sink=self._audio, prediction_sink=self._prediction)
                with self._condition:
                    self._stats['processed_chunks'] += 1
                    self._stats['processed_samples'] += len(pcm)
                del pcm
            with self._condition:
                clean = not self._error and not self._cancelled
            if clean:
                pipeline_state = self.pipeline.finish(self._audio)
        except Exception as exc:
            with self._condition:
                self._error = self._error or type(exc).__name__+': '+str(exc)
        finally:
            with self._condition:
                clean = not self._error and not self._cancelled
            try:
                session = self.writer.close(completed=clean, pipeline_state=pipeline_state)
            except Exception as exc:
                with self._condition:
                    self._error = self._error or 'OUTPUT_CLOSE: '+str(exc)
            with self._condition:
                clean = not self._error and not self._cancelled and session is not None
                self._queue.clear()
                self._stats['queued_items'] = self._stats['queued_bytes'] = 0
                self._stats['unprocessed_accepted_samples'] = self._stats['accepted_samples']-self._stats['processed_samples']
                self._result = dict(status='COMPLETED' if clean else ('CANCELED' if self._cancelled else 'FAILED'),
                    error=self._error, statistics=dict(self._stats), session=session,
                    pipeline_state=pipeline_state, board_execution=False)
                self._input_closed = True
                self._condition.notify_all()
