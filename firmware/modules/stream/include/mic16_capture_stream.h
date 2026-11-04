#ifndef MIC16_CAPTURE_STREAM_H
#define MIC16_CAPTURE_STREAM_H
#include "mic16_window_capture.h"
#include "mic16_stream_scheduler.h"
#ifdef __cplusplus
extern "C" {
#endif
struct m16_frame_front_stream;
struct m16_front_context_task;
typedef struct {
    mic16_stream_ticket ticket;
    mic16_stream_window window;
    const float *pcm;
    struct m16_front_context_task *prepared;
} m16_capture_stream_view;
typedef struct {
    unsigned state;
    m16_window_capture_lease lease;
    mic16_stream_ticket ticket;
} m16_capture_stream_binding;
typedef struct {
    unsigned magic,stopping;
    int error;
    m16_window_capture *capture;
    mic16_stream_scheduler *scheduler;
    m16_capture_stream_binding bindings[3];
    struct m16_frame_front_stream *front;
    struct m16_front_context_task *front_context;
    uint64_t next_context_first,context_received_tick,finish_sample;
    unsigned context_state;
} m16_capture_stream;
/* 真实采集运行时与调度器之间零整窗复制交接。三个PCM指针跟随采集租约，
 * 不能把scheduler.slot误当物理缓冲号。调度器只由同一事件循环调用，ISR不访问它。 */
int m16_capture_stream_init(m16_capture_stream *,m16_window_capture *,mic16_stream_scheduler *);
/* 同一逻辑整窗调度器的2048输入路径，后端仍按完整20帧消费。 */
int m16_capture_stream_init_front(m16_capture_stream *,struct m16_frame_front_stream *,
 struct m16_front_context_task *,mic16_stream_scheduler *);
int m16_capture_stream_start(m16_capture_stream *);
int m16_capture_stream_step(m16_capture_stream *);
int m16_capture_stream_take(m16_capture_stream *,m16_capture_stream_view *);
/* 调用前必须已停止所有使用该PCM的计算/增强；正常路径须先预留输出槽。
 * 该函数不会代替HLS排空；错误后也不得提前归还仍被设备读取的输入。 */
int m16_capture_stream_release(m16_capture_stream *,const m16_capture_stream_view *);
/* 正常停止只封住下一次整窗续接，继续交付已有窗，不中止scheduler时段。 */
int m16_capture_stream_request_finish(m16_capture_stream *);
/* 原始采集已取尽；队列里可能仍有READY/COMPUTE，须由总控继续排空。 */
int m16_capture_stream_input_complete(const m16_capture_stream *);
int m16_capture_stream_stop(m16_capture_stream *);
int m16_capture_stream_releasable(const m16_capture_stream *);
int m16_capture_stream_restart(m16_capture_stream *,unsigned epoch,uint64_t context_id,uint64_t reference_id);
#ifdef __cplusplus
}
#endif
#endif
