"""Managed PC file replay process. No Qt, network, training or board commands."""
from pathlib import Path
import os
for _key in ('OMP_NUM_THREADS', 'MKL_NUM_THREADS', 'OPENBLAS_NUM_THREADS'):
    os.environ[_key] = '2'
import hashlib
import json
import sys
import threading
import time

_print_lock = threading.Lock()


def emit(kind, **fields):
    with _print_lock:
        print(json.dumps(dict(kind=kind, **fields), ensure_ascii=False, allow_nan=False), flush=True)


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            value.update(block)
    return value.hexdigest()


def run(request_path):
    request_path = Path(request_path).resolve(strict=True)
    job = request_path.parent
    receipt_path = job/'receipt.json'
    if receipt_path.exists() or (job/'audio').exists():
        raise FileExistsError('Preserve existing job; choose a new output directory')
    worker = writer = None
    monitor = None
    monitor_stop = threading.Event()
    result = dict(status='FAILED', error='', board_execution=False, network_transport_tested=False,
                  new_acoustic_quality_metrics=False, training=0)
    try:
        if request_path.stat().st_size > 65536:
            raise ValueError('Request exceeds 64KiB')
        request = json.loads(request_path.read_text(encoding='utf-8-sig'))
        if (request['schema'] != 'mic16.per_source.file_job.v1' or
                request['input_encoding'] not in ('normalized_float32_fullscale_1','signed_int32_explicit_contract') or
                request['sample_rate_hz'] != 39062.5 or
                request['input_origin'] not in ('FILE_REPLAY', 'SIMULATED_REPLAY')):
            raise ValueError('Explicit normalized file-replay contract required; raw int32 is not implicitly scaled')
        pcm_path = (job/request['pcm_path']).resolve(strict=True)
        quiet_path = (job/request['quiet_path']).resolve(strict=True)
        if pcm_path == quiet_path:
            raise ValueError('Independent quiet reference required')
        emit('stage', message='核对文件与输入格式；不复制原始录音')
        import numpy as np
        pcm = np.load(pcm_path, mmap_mode='r', allow_pickle=False)
        quiet = np.load(quiet_path, mmap_mode='r', allow_pickle=False)
        raw_mode=request['input_encoding']=='signed_int32_explicit_contract'
        expected_dtype=np.dtype('<i4') if raw_mode else np.dtype('float32')
        if (pcm.dtype != expected_dtype or pcm.ndim != 2 or pcm.shape[1] != 16 or
                len(pcm) < 40960 or len(pcm) % 40960 or quiet.dtype != expected_dtype or
                quiet.shape not in ((8192,16),(4,2048,16))):
            raise ValueError('Need declared float32/int32 N×16 whole 40960-sample windows and separate 8192×16 quiet NPY')
        dependencies=[pcm_path,quiet_path];raw_contract=None
        if raw_mode:
            from raw_pcm_input import RawPcmContract,NormalizedRawView
            contract=RawPcmContract(job/request['raw_contract_path'],input_origin=request['input_origin'])
            dependencies+=contract.dependencies;raw_contract=contract.receipt()
            quiet=contract.convert(quiet.reshape(8192,16));pcm=NormalizedRawView(pcm,contract)
        sources = {str(p):digest(p) for p in dependencies}
        result['raw_pcm_contract']=raw_contract
        emit('stage', message='加载冻结C16和原增强权重')
        import torch
        torch.set_num_threads(2)
        torch.use_deterministic_algorithms(True)
        from continuous_per_source import ContinuousPerSourcePipeline, PerSourceWaveWriter
        from input_worker import ContinuousInputWorker
        pipe = ContinuousPerSourcePipeline(quiet, pose_key=request['pose_key'], segment_id=request['segment_id'],
            start_time_s=request['start_time_s'], data_origin=request['input_origin'])
        writer = PerSourceWaveWriter(job/'audio', segment_id=request['segment_id'],
            pose_key=request['pose_key'], evidence_source=request['input_origin'])
        journal = (job/'window_results.jsonl').open('x', encoding='utf-8')

        def record(index, output, prediction):
            # 全部周期保存到磁盘，GUI只合并最新小型视图。
            row = dict(cycle=index+1, tracks=output['detection']['tracks'],
                candidate_logits=output['detection']['candidate_logits'],
                source_audio_masks_sha256=[hashlib.sha256(mask.tobytes()).hexdigest() for mask in prediction['masks']])
            journal.write(json.dumps(row, ensure_ascii=False, allow_nan=False)+'\n')
            journal.flush()

        worker = ContinuousInputWorker(pipe, writer, prediction_sink=record).start()

        def watch():
            revision = -1
            while not monitor_stop.wait(.05):
                if (job/'cancel.request').exists():
                    worker.cancel()
                stats, progress = worker.snapshot()
                if progress is not None and stats['progress_revision'] != revision:
                    revision = stats['progress_revision']
                    emit('progress', **progress)

        monitor = threading.Thread(target=watch, name='mic16-progress-only', daemon=True)
        monitor.start()
        started = time.perf_counter()
        emit('stage', message='后台按序处理，所有确认源分别保存')
        try:
            for begin in range(0, len(pcm), 4096):
                end = min(len(pcm), begin+4096)
                if not worker.submit(pcm[begin:end], first_sample=begin,
                    audio_start_s=request['start_time_s']+begin/39062.5,
                    audio_end_s=request['start_time_s']+end/39062.5, contiguous=True, block=True):
                    break
            worker.finish_input()
            worker.wait()
            result.update(worker.result, input_files_sha256=sources,
                input_samples=len(pcm), elapsed_processing_and_file_write_s=time.perf_counter()-started,
                file_replay_backpressure=True, input_files_copied=False)
            # 文件被改动时不能标记为可复现的已完成任务。
            unchanged = all(digest(Path(path)) == wanted for path,wanted in sources.items())
            result['input_files_unchanged'] = unchanged
            if not unchanged:
                result.update(status='FAILED', error='Input file changed during processing')
                session = job/'audio/session.json'
                info = json.loads(session.read_text(encoding='utf-8'))
                info['status'] = 'INCOMPLETE'
                save(session, info)
                result['session']['status'] = 'INCOMPLETE'
            if result['status'] == 'COMPLETED' and result['statistics']['processed_samples'] != len(pcm):
                raise ValueError('Input not fully processed')
        finally:
            if worker.result is None:
                worker.cancel()
                worker.wait()
            journal.close()
    except Exception as error:
        result.update(status='FAILED', error=type(error).__name__+': '+str(error))
        if worker is not None:
            worker.cancel()
            worker.wait()
        elif writer is not None and not writer.closed:
            writer.close(completed=False)
    finally:
        monitor_stop.set()
        if monitor is not None:
            monitor.join()
        result.update(schema='mic16.per_source.file_job.result.v1',
            audio_directory='audio', input_origin=locals().get('request',{}).get('input_origin','UNKNOWN'))
        save(receipt_path, result)
        emit('finished', status=result['status'], error=result['error'])
    return 0 if result['status'] == 'COMPLETED' else (2 if result['status'] == 'CANCELED' else 1)


if __name__ == '__main__':
    raise SystemExit(run(sys.argv[1]))
