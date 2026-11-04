"""有界的定位演示计时记录；Qt绘制完成不代表显示器已出光。"""
from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
import json
import math
import statistics
import time
import uuid


def distribution(values):
    data = sorted(values)
    if not data:
        return dict(count=0, mean_ms=None, p95_ms=None, maximum_ms=None)
    return dict(count=len(data), mean_ms=statistics.fmean(data),
                p95_ms=data[max(0, math.ceil(.95 * len(data)) - 1)], maximum_ms=data[-1])


class DisplayDemoTrace:
    """热路径仅追加小记录；退出或显式导出时落盘，不保存音频数组。"""
    def __init__(self, root, *, maximum_events=4096):
        if not 8 <= maximum_events <= 16384:
            raise ValueError('演示计时记录上限无效')
        self.root = Path(root)
        self.maximum_events = maximum_events
        self.events = []
        self.dropped_events = 0
        self.path = None

    def add(self, kind, *, at_ns=None, **fields):
        if len(self.events) >= self.maximum_events:
            self.dropped_events += 1
            return
        self.events.append(dict(kind=kind, at_ns=time.monotonic_ns() if at_ns is None else at_ns, **fields))

    def direction(self, frame):
        self.add('direction_applied', key=list(frame.key), wire_version=getattr(frame,'wire_version',2), valid=frame.valid,
                 reason=frame.reason, azimuth_deg=frame.azimuth_deg, elevation_deg=frame.elevation_deg,
                 coherence=frame.coherence,contrast=frame.contrast,support_fraction=frame.support_fraction,
                 received_ns=round(frame.received_monotonic_s * 1e9),
                 first_received_tick=frame.first_received_tick, last_front_tick=frame.last_front_tick,
                 completed_tick=frame.completed_tick, tick_hz=frame.tick_hz)

    def mark_phase(self,label):
        if not isinstance(label,str) or not label.strip() or len(label.strip())>80:
            raise ValueError('测试阶段名称需为1～80个字符')
        self.add('phase_marker',label=label.strip(),physical_event_time_verified=False,
                 angular_ground_truth_verified=False)

    def summary(self):
        applied = {tuple(e['key']): e for e in self.events if e['kind'] == 'direction_applied'}
        painted, last_epoch, last_time, last_sequence = set(), None, None, None
        receipt_to_apply, receipt_to_paint, intervals, front_to_done, tail, missing = [], [], [], [], [], 0
        for row in applied.values():
            receipt_to_apply.append((row['at_ns'] - row['received_ns']) / 1e6)
            front_to_done.append((row['completed_tick'] - row['first_received_tick']) * 1000 / row['tick_hz'])
            tail.append((row['completed_tick'] - row['last_front_tick']) * 1000 / row['tick_hz'])
        for row in self.events:
            if row['kind'] != 'direction_painted':
                continue
            key = tuple(row['key'])
            if key not in applied or key in painted:
                continue
            painted.add(key)
            receipt_to_paint.append((row['at_ns'] - applied[key]['received_ns']) / 1e6)
            epoch = key[:2]
            if epoch == last_epoch:
                intervals.append((row['at_ns'] - last_time) / 1e6)
                missing += max(0, key[2] - last_sequence - 1)
            last_epoch, last_time, last_sequence = epoch, row['at_ns'], key[2]
        coverage_end, coverage_gaps, uncovered_samples = {}, [], 0
        for row in applied.values():
            key=tuple(row['key']);epoch=key[:2];previous=coverage_end.get(epoch,0)
            if key[3]>previous:
                uncovered_samples+=key[3]-previous
                if len(coverage_gaps)<64:coverage_gaps.append(dict(boot=key[0],epoch=key[1],first_sample=previous,end_sample=key[3]))
            coverage_end[epoch]=max(previous,key[4])
        raw_end={}
        for row in self.events:
            if row['kind']=='receiver' and row.get('raw_boot') and row.get('raw_epoch') and row.get('raw_frame') is not None:
                epoch=(row['raw_boot'],row['raw_epoch'])
                raw_end[epoch]=max(raw_end.get(epoch,0),(row['raw_frame']+1)*2048)
        tail_samples=sum(max(0,end-coverage_end.get(epoch,0)) for epoch,end in raw_end.items())
        heatmaps = {tuple(e['key']): e for e in self.events if e['kind'] == 'heatmap_painted'}
        response_to_paint = [(e['at_ns'] - e['response_received_ns']) / 1e6 for e in heatmaps.values()]
        video_overlays={tuple(e['key']):e for e in self.events if e['kind']=='video_overlay_painted' and tuple(e['key']) in applied}
        video_receipt_to_paint=[(e['at_ns']-e['video_received_ns'])/1e6 for e in video_overlays.values()]
        acoustic_to_overlay=[(e['at_ns']-e['acoustic_received_ns'])/1e6 for e in video_overlays.values()]
        return dict(
            event_count=len(self.events), dropped_events=self.dropped_events,
            direction_applied_count=len(applied), direction_painted_count=len(painted),
            valid_direction_count=sum(bool(e['valid']) for e in applied.values()),
            observed_gui_sequence_gaps=missing,
            observed_result_uncovered_samples=uncovered_samples,observed_result_gap_examples=coverage_gaps,
            observed_result_tail_samples=tail_samples,observed_uncovered_including_tail=uncovered_samples+tail_samples,
            gap_causes_include_scheduled_analysis_skips_and_network_or_GUI_omissions=True,
            receipt_to_apply=distribution(receipt_to_apply), receipt_to_qt_paint=distribution(receipt_to_paint),
            heatmap_painted_count=len(heatmaps), response_receipt_to_qt_paint=distribution(response_to_paint),
            qt_new_sample_paint_intervals=distribution(intervals),
            video_overlay_painted_count=len(video_overlays),
            video_receipt_to_overlay_paint=distribution(video_receipt_to_paint),
            acoustic_receipt_to_video_overlay_paint=distribution(acoustic_to_overlay),
            manual_phase_markers=[e for e in self.events if e['kind']=='phase_marker'],
            board_first_DMA_handoff_to_result=distribution(front_to_done),
            board_last_front_to_result=distribution(tail),
            complete_A_compute_ms=None, physical_sound_to_screen_ms=None,
            cross_device_clock_mapping_verified=False, realtime_qualified=False,
            definitions=dict(
                board_first_DMA_handoff_to_result='首子帧DMA交接至结果，含收齐四帧及调度等待，不能称CPU计算耗时',
                board_last_front_to_result='最后子帧前端完成至结果，只是末段耗时',
                receipt_to_qt_paint='同一PC单调时钟：收包至Qt绘制返回；不含显示器扫描输出',
                acoustic_receipt_to_video_overlay_paint='同一PC时钟：声学结果接收至对应响应叠加到视频的Qt绘制返回；不证明物理声像同步',
                video_receipt_to_overlay_paint='被选中视频帧到达至叠加绘制返回；不含相机曝光、USB前段或显示器扫描',
                observed_gui_sequence_gaps='界面实际绘制的新结果序号缺口，不能直接归因为网络丢包'),
        )

    def save(self):
        if self.path is None:
            name = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ_') + uuid.uuid4().hex[:8]
            self.path = self.root / name
            self.path.mkdir(parents=True, exist_ok=False)
        with (self.path / 'events.jsonl').open('w', encoding='utf-8') as stream:
            for row in self.events:
                stream.write(json.dumps(row, ensure_ascii=False) + '\n')
        result = self.summary()
        (self.path / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        return self.path
