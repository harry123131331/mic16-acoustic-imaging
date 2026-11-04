#ifndef MIC16_LATEST_WINDOW_TASK_H
#define MIC16_LATEST_WINDOW_TASK_H
#include "mic16_pcm_acquisition.h"
#include "mic16_front_ring.h"
#include "mic16_fast_doa_task.h"

#define M16_LW_RECORDS 64u
#define M16_FR_LATEST 8u
enum {M16_LW_IDLE=0,M16_LW_COPY=1,M16_LW_COMPUTE=2,M16_LW_OUTPUT=3,M16_LW_STOPPING=4,M16_LW_STOPPED=5};
enum {M16_LW_INVALID=-2320,M16_LW_SEQUENCE=-2321,M16_LW_CLOCK=-2322};
typedef struct {float pcm[4][M16_PA_VALUES];} m16_latest_window_storage;
typedef struct {
 uint64_t epoch,sequence,first_frame,end_frame,gap_first,gap_end;
 uint64_t copy_started_tick,snapshot_tick,completed_tick;
 unsigned completed,cancelled;
} m16_latest_window_record;
typedef struct {
 unsigned magic,active,phase,copy_frame,copy_cursor,next_front,front_busy,front_ready,coarse_busy,next_doa;
 int error;
 uint64_t epoch,last_tick,selected_first,analyzed_end,skipped_frames,analyzed_frames;
 uint64_t selections,snapshot_retries,copy_steps,results,records_evicted,cancelled_jobs;
 m16_pcm_frame_metadata metadata[4];
 m16_latest_window_record current,records[M16_LW_RECORDS];
 m16_pcm_acquisition *producer;
 m16_front_ring *ring;
 m16_fast_doa_task *direction;
 m16_latest_window_storage *storage;
} m16_latest_window_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 只复用原公共谱/粗扫任务；4帧独立快照不会回写采集PCM环。 */
int m16_latest_window_init(m16_latest_window_task *,m16_pcm_acquisition *,m16_front_ring *,m16_fast_doa_task *,m16_latest_window_storage *);
int m16_latest_window_start(m16_latest_window_task *,uint64_t epoch,uint64_t tick);
/* 每步至多复制1KiB，并推进一个原公共谱/粗扫/方向片段；不等待整组完成。 */
int m16_latest_window_step(m16_latest_window_task *,uint64_t tick);
/* UDP三片全部交接后才ACK；保留原A2_H4序号，不用采样起点伪造序号。 */
int m16_latest_window_ack(m16_latest_window_task *,uint64_t epoch,uint64_t sequence);
int m16_latest_window_stop(m16_latest_window_task *);
int m16_latest_window_releasable(const m16_latest_window_task *);
#ifdef __cplusplus
}
#endif
#endif
