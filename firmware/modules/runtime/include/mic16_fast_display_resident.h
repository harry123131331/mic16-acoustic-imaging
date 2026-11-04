#ifndef MIC16_FAST_DISPLAY_RESIDENT_H
#define MIC16_FAST_DISPLAY_RESIDENT_H
#include "mic16_fast_doa_front.h"
#include "mic16_pipeline_front_stream.h"
#include "mic16_fast_display_wire.h"
#include "mic16_pcm_wire.h"
#include "mic16_resident_control.h"

/* 只用于显式A单独功能/计时候选。B原入口保留，并发准入另行完成，禁止冒称B已运行。 */
typedef struct {
 unsigned magic,state,wire_part,wire_pending,wire_bytes,raw_copying,raw_cursor,raw_head,raw_tail,raw_count,raw_sending,raw_waiting;
 int error;
 uint64_t wire_wait_tick,raw_wait_tick,raw_next_frame,raw_copied,raw_sent,raw_packets,wire_packets,wire_backpressure;
 uint64_t discarded_raw,discarded_results,iterations,longest_step_ticks,latest_sequence;
 /* 0共享后端、1采集/公共前端、2方向、3RAW复制、4UDP；均为板端原始计时器刻度。 */
 uint64_t service_total_ticks[5],service_max_ticks[5];
 uint64_t first_output_tick,last_output_tick,max_output_gap_ticks;
 float raw_pcm[2][32768];
 uint64_t raw_frame[2];
 unsigned char packet[M16_FDW_MAX_BYTES];
 m16_pcm_wire raw_wire;
 m16_pipeline_front_stream *stream;
 m16_fast_doa_front frontend;
 m16_resident_control *control;
 void *context;
 uint64_t tick_hz;
 uint64_t (*ticks)(void *);
 int (*send)(void *,unsigned,const unsigned char *,unsigned);
 int (*backend)(void *);
 void *backend_context;
} m16_fast_display_resident;
#ifdef __cplusplus
extern "C" {
#endif
int m16_fast_display_resident_init(m16_fast_display_resident *,m16_pipeline_front_stream *,
 m16_fast_doa_task *,m16_resident_control *,uint64_t tick_hz,void *context,
 uint64_t (*ticks)(void *),int (*send)(void *,unsigned,const unsigned char *,unsigned),
 int (*backend)(void *),void *backend_context);
int m16_fast_display_resident_step(m16_fast_display_resident *);
#ifdef __cplusplus
}
#endif
#endif
