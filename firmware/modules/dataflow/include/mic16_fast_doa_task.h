#ifndef MIC16_FAST_DOA_TASK_H
#define MIC16_FAST_DOA_TASK_H
#include <stdint.h>

#define M16_FD_BANDS 24u
#define M16_FD_GRID 325u
#define M16_FD_HISTORY 4u
#define M16_FD_ALGORITHM 0xa204u
enum {M16_FD_IDLE=0, M16_FD_MAX=1, M16_FD_SECOND=2, M16_FD_ADD=3,
      M16_FD_NORMALIZE=4, M16_FD_FUSE=5, M16_FD_PEAK=6, M16_FD_CONTRAST=7};
enum {M16_FD_INVALID=-1800,M16_FD_BUSY=-1801,M16_FD_SEQUENCE=-1802,
      M16_FD_VALUE=-1803,M16_FD_CLOCK=-1804};
enum {M16_FD_VALID=0,M16_FD_NO_BAND=1,M16_FD_LOW_QUALITY=2};

typedef struct {
 uint64_t epoch,sequence,first_frame,end_frame,first_sample,end_sample;
 uint64_t first_received_tick,last_front_tick,completed_tick;
 unsigned valid,map_valid,reason,grid,selected_bands[4],support_frames;
 double azimuth_deg,elevation_deg,coherence,contrast,support_fraction;
 /* 直接来自四个已归一化帧的空间响应，禁止以方向点另造热图。 */
 float response[M16_FD_GRID];
} m16_fast_doa_result;

typedef struct {
 unsigned magic,active,phase,error,output_ready,band,cursor,history_count;
 unsigned band_grid,frame_grid,frame_valid[4],frame_peak[4],band_counts[4];
 uint64_t epoch,next_frame,sequence,last_tick,group_received_tick,last_front_tick;
 uint64_t frames_consumed,results_produced,busy_count,steps;
 double maximum_level,band_peak,second,weight,total,frame_max,coherence,contrast;
 double levels[24],history[4][325],fused[325];
 /* begin一次有界复制31.2KiB，不借用SRP指针，也不申请B的20帧reader。 */
 float input[24][325];
 m16_fast_doa_result result;
} m16_fast_doa_task;

#ifdef __cplusplus
extern "C" {
#endif
int m16_fast_doa_init(m16_fast_doa_task *);
int m16_fast_doa_start(m16_fast_doa_task *,uint64_t epoch,uint64_t tick);
int m16_fast_doa_can_accept(const m16_fast_doa_task *);
/* 帧号从0严格递增；输入被复制后立即可归还。错误/缺帧必须由常驻层公开报告。 */
int m16_fast_doa_begin(m16_fast_doa_task *,const float *,const double *,uint64_t epoch,
 uint64_t frame,uint64_t first_sample,uint64_t received_tick,uint64_t front_done_tick);
/* 运算每步最多64方向项；发布时一次复制325个F32，不调用FFT、SRP设备或C16。 */
int m16_fast_doa_step(m16_fast_doa_task *,uint64_t tick);
int m16_fast_doa_peek(const m16_fast_doa_task *,const m16_fast_doa_result **);
int m16_fast_doa_ack(m16_fast_doa_task *,uint64_t epoch,uint64_t sequence);
int m16_fast_doa_stop(m16_fast_doa_task *);
#ifdef __cplusplus
}
#endif
#endif
