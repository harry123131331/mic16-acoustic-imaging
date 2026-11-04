"""Bounded continuous audio with frozen C16 IDs and explicit sample intervals.

Uses the existing global-grid synthesis algorithm; never uses the older spatial
tracker, changes detector policy, or treats inactive zero padding as capture.
"""
from pathlib import Path
import ast
import hashlib
import json
import math
import re
import sys
import types

from per_source_reference import (
    ROOT, PACKAGE, FrozenPerSourcePCReference, weights_reference, normalized_pcm,
    FS, WINDOW, DURATION,
)
import numpy as np
import soundfile as sf


def _load_reference(path, name, bindings, skipped_imports=()):
    """Wire existing modules in a private namespace without editing their source.

    Two old runtime modules use bare imports. Only import wiring is replaced;
    function/class AST bodies remain unchanged. No host module is overwritten.
    """
    tree=ast.parse(path.read_text(encoding='utf-8-sig'),filename=str(path))
    tree.body=[node for node in tree.body if not
        (isinstance(node,ast.ImportFrom) and node.module in skipped_imports)]
    module=types.ModuleType(name);module.__file__=str(path)
    module.__dict__.update(bindings)
    exec(compile(tree,str(path),'exec'),module.__dict__)
    return module


RUNTIME=ROOT/'python_tools/runtime'
window_reference=_load_reference(RUNTIME/'mic16_window_stream.py','_c16_window_source_reference',{})
offline_reference=_load_reference(RUNTIME/'mic16_continuous_audio.py','_c16_global_audio_reference',
    {'lagrange5_coefficients':weights_reference.lagrange5_coefficients},
    ('speech_enhancement.beamforming_convention',))
_names=('SpatialTracks','steered_segment','interpolate_mask','ramp','N','FS','UP','DOWN','RATE',
        'FADE_SAMPLES','HISTORY_SAMPLES')
bindings={name:getattr(offline_reference,name) for name in _names}
bindings['Mic16WindowStream']=window_reference.Mic16WindowStream
bounded_reference=_load_reference(RUNTIME/'mic16_bounded_audio.py','_c16_bounded_audio_reference',bindings,
    ('mic16_window_stream','mic16_continuous_audio'))


def synthesis_prediction(audio):
    streams=audio['streams']
    return bounded_reference.snapshot_prediction(
        [s['direction'] for s in streams],[s['source_id'] for s in streams],
        np.asarray([s['mask'] for s in streams]).reshape(len(streams),67,64),
        audio.get('channel_weights',np.empty((0,16))))


class ContinuousPerSourcePipeline:
    """One PCM input buffer, at most three synthesis blocks, synchronous sinks.

    A transport gap fails the segment. Caller starts a NEW segment/epoch rather
    than filling missing captured audio or reusing IDs on the old timeline.
    """
    def __init__(self, quiet_reference, *, pose_key, segment_id, start_time_s=0., engine=None,
                 data_origin='FILE_REPLAY'):
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',segment_id) or not math.isfinite(start_time_s):
            raise ValueError('Explicit simple segment ID and finite start time required')
        self.quiet=normalized_pcm(quiet_reference,((8192,16),(4,2048,16))).reshape(4,2048,16).copy()
        self.quiet.setflags(write=False)
        self.engine=engine or FrozenPerSourcePCReference(pose_key=pose_key)
        if data_origin not in ('SIMULATED_REPLAY','FILE_REPLAY'):raise ValueError('Explicit PC input origin required')
        self.pose_key=pose_key;self.segment_id=segment_id;self.start_time=float(start_time_s)
        self.data_origin=data_origin
        self.audio=bounded_reference.BoundedAudio()
        self.buffer=np.empty((WINDOW,16),np.float32)
        self.pending=self.expected_sample=self.completed=0
        self.failed=self.closed=False;self._metadata={}
        self.max_metadata=0

    def _annotate(self, packet):
        if packet is None:return None
        metadata=self._metadata.pop(packet['window'])
        packet.update(schema_version=1,segment_id=self.segment_id,pose_key=self.pose_key,
            timestamp_domain='relative_input_audio_clock_not_camera_calibrated',
            segment_audio_start_s=self.start_time,
            output_begin_time_s=self.start_time+packet['output_start_sample']/16000.,
            output_end_time_s=self.start_time+packet['output_end_sample_exclusive']/16000.,
            current_confirmed=metadata,transition_audio_is_not_current_detection=True,
            data_origin='PC_REPLAY_RESULT')
        return packet

    def push(self, chunk, *, first_sample, audio_start_s, audio_end_s, contiguous,
             audio_sink, prediction_sink=None, sample_rate=FS):
        if self.failed or self.closed:raise RuntimeError('Segment closed or failed; create a new epoch')
        try:
            if contiguous is not True or type(first_sample) is not int or first_sample!=self.expected_sample or sample_rate!=FS:
                raise ValueError('Gap/duplicate/rate mismatch; segment must not be stitched')
            values=np.asarray(chunk)
            if values.ndim!=2 or values.shape[1]!=16 or not 1<=len(values)<=WINDOW:
                raise ValueError('Chunk must be 1..40960 by 16')
            x=normalized_pcm(values,(values.shape,))
            begin=self.start_time+first_sample/FS
            if not all(math.isfinite(t) for t in (audio_start_s,audio_end_s)) or abs(begin-audio_start_s)>1e-6 or abs(audio_end_s-audio_start_s-len(x)/FS)>1e-6:
                raise ValueError('Input sample/time intervals do not agree')
            if getattr(self.engine.detector,'pose_key',self.pose_key)!=self.pose_key:
                raise ValueError('Array pose changed; start a new epoch')
            offset=0;finished=0
            while offset<len(x):
                take=min(WINDOW-self.pending,len(x)-offset)
                self.buffer[self.pending:self.pending+take]=x[offset:offset+take]
                offset+=take;self.pending+=take;self.expected_sample+=take
                if self.pending!=WINDOW:continue
                index=self.completed;start=self.start_time+index*DURATION
                output=self.engine.infer_window(self.buffer,self.quiet,sample_rate=FS,
                    audio_start_s=start,audio_end_s=start+DURATION,data_origin=self.data_origin,contiguous=True)
                prediction=synthesis_prediction(output['source_audio'])
                self._metadata[index]=[{k:s[k] for k in ('source_id','direction','candidate_index')}
                    for s in output['source_audio']['streams']]
                self.max_metadata=max(self.max_metadata,len(self._metadata))
                if prediction_sink is not None:prediction_sink(index,output,prediction)
                packet=self.audio.accept(self.buffer,prediction)
                self.completed+=1;self.pending=0;finished+=1
                if packet is not None:audio_sink(self._annotate(packet))
                assert len(self._metadata)<=1
            return dict(accepted_samples=len(x),completed_windows=finished)
        except Exception:
            self.failed=True
            self.engine.detector.reset_segment()
            raise

    def finish(self, audio_sink, *, discard_tail=False):
        if self.failed or self.closed:raise RuntimeError('Segment closed or failed')
        if self.pending and not discard_tail:
            raise ValueError('Incomplete observation; continue input or explicitly discard tail, never pad')
        tail=self.pending;self.pending=0
        try:
            # 合成缓存清空前保留最后一份标注。
            packet=self.audio.finish()
            if packet is not None:audio_sink(self._annotate(packet))
            self.closed=True
        except Exception:
            self.failed=True;raise
        return dict(schema_version=1,segment_id=self.segment_id,pose_key=self.pose_key,
            completed_windows=self.completed,discarded_input_tail_samples=tail,
            accepted_input_samples=self.expected_sample,
            output_samples=(self.completed*WINDOW*256+624)//625,
            audio_state=self.audio.state(),input_buffer_bytes=self.buffer.nbytes,
            quiet_reference_bytes=self.quiet.nbytes,maximum_annotation_records=self.max_metadata,
            one_observed_window_lookahead=True,network_transport_tested=False,board_execution=False)


class PerSourceWaveWriter:
    """FLOAT WAVs on one shared 16kHz timeline, bounded working memory.

    No persistent per-track array/file handles. Inactive-source zero regions
    align files, and packet metadata distinguishes them from measured activity.
    Missing transport samples are rejected, never filled here.
    """
    def __init__(self, directory, *, segment_id, pose_key, evidence_source='SIMULATED_REPLAY'):
        self.directory=Path(directory)
        if self.directory.exists():raise FileExistsError('Preserve previous session')
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',segment_id):raise ValueError('Simple segment ID required')
        self.directory.mkdir(parents=True)
        self.segment_id=segment_id;self.pose_key=pose_key;self.evidence_source=evidence_source
        self.next_sample=self.packets=0;self.closed=self.failed=False
        self._events=(self.directory/'packets.jsonl').open('x',encoding='utf-8')
        self._zeros=np.zeros(4096,np.float32)
        self.max_zero_buffer_bytes=self._zeros.nbytes

    def _append(self,path,begin,values):
        if not path.exists():
            with sf.SoundFile(path,'w',samplerate=16000,channels=1,subtype='FLOAT'):pass
        with sf.SoundFile(path,'r+') as stream:
            if stream.samplerate!=16000 or stream.channels!=1 or stream.frames>begin:
                raise ValueError('Existing audio interval would overlap or use a different format')
            stream.seek(0,sf.SEEK_END)
            missing=begin-stream.frames
            while missing:
                n=min(missing,len(self._zeros));stream.write(self._zeros[:n]);missing-=n
            stream.write(np.asarray(values,np.float32))

    def accept(self,packet):
        if self.closed or self.failed:raise RuntimeError('Writer closed or failed')
        try:
            begin=int(packet['output_start_sample']);end=int(packet['output_end_sample_exclusive'])
            if packet['segment_id']!=self.segment_id or packet['pose_key']!=self.pose_key or begin!=self.next_sample or end<=begin or packet['output_rate']!=16000:
                raise ValueError('Segment/source timeline mismatch, duplicate or missing packet')
            signals=[packet['input_MIC0']]+[s[k] for s in packet['tracks'].values() for k in ('WDAS','enhanced')]
            if any(np.asarray(v).shape!=(end-begin,) or not np.isfinite(v).all() for v in signals):
                raise ValueError('All source packets must have the SAME finite sample interval')
            self._append(self.directory/'input_MIC0.wav',begin,packet['input_MIC0'])
            files=[]
            for identity,source in sorted(packet['tracks'].items()):
                if type(identity) is not int or identity<1:raise ValueError('Positive source ID required')
                for kind in ('WDAS','enhanced'):
                    name=f'source_{identity:04d}_{kind}.wav'
                    self._append(self.directory/name,begin,source[kind])
                    files.append(dict(source_id=identity,kind=kind,path=name))
            row={k:packet[k] for k in ('schema_version','segment_id','pose_key','window',
                'output_start_sample','output_end_sample_exclusive','output_rate','output_begin_time_s',
                'output_end_time_s','current_confirmed','transition_audio_is_not_current_detection','final')}
            row.update(files=files,evidence_source=self.evidence_source)
            self._events.write(json.dumps(row,ensure_ascii=False,allow_nan=False)+'\n');self._events.flush()
            self.next_sample=end;self.packets+=1
        except Exception:
            self.failed=True;raise

    def close(self, *, completed=True, pipeline_state=None):
        if self.closed:raise RuntimeError('Writer already closed')
        self.closed=True;self._events.close()
        finished=completed and not self.failed
        assets=0
        if finished:
            # 显式对齐非活动声源，不填补传输缺口。
            for path in self.directory.glob('source_*.wav'):
                self._append(path,self.next_sample,np.empty(0,np.float32))
        with (self.directory/'audio_files.jsonl').open('x',encoding='utf-8') as manifest:
            for path in self.directory.glob('*.wav'):
                info=sf.info(path)
                identity=int(path.name.split('_')[1]) if path.name.startswith('source_') else None
                kind=path.stem.rsplit('_',1)[-1] if identity is not None else 'input_MIC0'
                digest=hashlib.sha256()
                with path.open('rb') as audio_file:
                    for chunk in iter(lambda:audio_file.read(1024*1024),b''):digest.update(chunk)
                manifest.write(json.dumps(dict(path=path.name,source_id=identity,kind=kind,
                    frames=info.frames,sample_rate=info.samplerate,
                    sha256=digest.hexdigest()),ensure_ascii=False)+'\n')
                assets+=1
        session=dict(schema_version=1,segment_id=self.segment_id,pose_key=self.pose_key,
            evidence_source=self.evidence_source,status='COMPLETED' if finished else 'INCOMPLETE',
            audio_rate=16000,output_samples=self.next_sample,packets=self.packets,audio_files=assets,
            activity_metadata='packets.jsonl',audio_manifest='audio_files.jsonl',
            zero_padding_means_inactive_source_not_captured_sound=True,
            maximum_zero_write_buffer_bytes=self.max_zero_buffer_bytes,
            pipeline_state=pipeline_state,board_execution=False)
        (self.directory/'session.json').write_text(json.dumps(session,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        return session
