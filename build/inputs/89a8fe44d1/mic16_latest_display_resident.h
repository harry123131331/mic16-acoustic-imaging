#ifndef MIC16_LATEST_DISPLAY_RESIDENT_H
#define MIC16_LATEST_DISPLAY_RESIDENT_H
#include "mic16_latest_window_task.h"
#include "mic16_latest_display_wire.h"
#include "mic16_pcm_wire.h"
#include "mic16_resident_control.h"
typedef struct {
 unsigned magic,state,wire_part,wire_pending,wire_bytes,raw_copying,raw_cursor;
 unsigned raw_head,raw_tail,raw_count,raw_sending,raw_waiting;
 int error;
 uint64_t tick_hz,wire_wait_tick,raw_wait_tick,raw_next_frame,raw_copied,raw_sent,raw_packets;
 uint64_t wire_packets,wire_backpressure,discarded_raw,discarded_results,iterations,longest_step_ticks;
 uint64_t latest_sequence,first_output_tick,last_output_tick,max_output_gap_ticks,last_compute_ticks;
 /* 0采集、1共享设备、2RAW复制、3UDP、4定位；计数包含IRQ扰动，不是纯核耗时。 */
 uint64_t service_total_ticks[5],service_max_ticks[5];
 uint64_t raw_frame[2];float raw_pcm[2][M16_PA_VALUES];
 unsigned char packet[M16_FDW_MAX_BYTES];
 m16_pcm_wire raw_wire;
 m16_pcm_acquisition producer;
 m16_latest_window_task analysis;
 m16_latest_window_storage snapshot;
 m16_resident_control *control;
 void *context,*backend_context;
 uint64_t (*ticks)(void *);
 int (*send)(void *,unsigned,const unsigned char *,unsigned);
 int (*backend)(void *);
} m16_latest_display_resident;
#ifdef __cplusplus
extern "C" {
#endif
int m16_latest_display_resident_init(m16_latest_display_resident *,m16_window_capture *,m16_front_ring *,
 m16_fast_doa_task *,m16_resident_control *,uint64_t tick_hz,void *context,uint64_t (*ticks)(void *),
 int (*send)(void *,unsigned,const unsigned char *,unsigned),int (*backend)(void *),void *backend_context);
int m16_latest_display_resident_step(m16_latest_display_resident *);
#ifdef __cplusplus
}
#endif
#endif
