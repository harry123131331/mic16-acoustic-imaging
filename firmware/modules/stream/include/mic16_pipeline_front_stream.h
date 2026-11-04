#ifndef MIC16_PIPELINE_FRONT_STREAM_H
#define MIC16_PIPELINE_FRONT_STREAM_H
#include "mic16_front_pipeline.h"
#include "mic16_window_capture.h"
typedef struct {
 unsigned magic,held,stopping;
 int error;
 uint64_t frames_taken,leases_released,credit_end,credit_stalls;
 m16_window_capture_lease lease;
 m16_window_capture *capture;
 m16_front_pipeline *front;
} m16_pipeline_front_stream;
#ifdef __cplusplus
extern "C" {
#endif
/* 持续消费同一个2048 DMA对象；不创建第二套驱动，不提供先采后算模式。 */
int m16_pipeline_front_stream_init(m16_pipeline_front_stream *,m16_window_capture *,m16_front_pipeline *);
int m16_pipeline_front_stream_start(m16_pipeline_front_stream *,unsigned epoch);
int m16_pipeline_front_stream_step(m16_pipeline_front_stream *);
int m16_pipeline_front_stream_stop(m16_pipeline_front_stream *);
int m16_pipeline_front_stream_releasable(const m16_pipeline_front_stream *);
/* 额度只限制接收，不能停止采样或隐瞒DMA过载；仅停止态可降低上限。 */
int m16_pipeline_front_stream_limit(m16_pipeline_front_stream *,uint64_t end);
int m16_pipeline_front_stream_restart(m16_pipeline_front_stream *);
#ifdef __cplusplus
}
#endif
#endif
