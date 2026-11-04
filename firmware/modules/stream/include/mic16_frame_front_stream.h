#ifndef MIC16_FRAME_FRONT_STREAM_H
#define MIC16_FRAME_FRONT_STREAM_H
#include "mic16_window_capture.h"
#include "mic16_front_ring.h"
typedef struct m16_frame_front_stream {
 unsigned magic,held,stopping;
 int error;
 uint64_t frames_taken,leases_released,front_while_dma,frame_limit,credit_stalls;
 m16_window_capture_lease lease;
 m16_window_capture *capture;
 m16_front_ring *ring;
 unsigned capture_once,prefilled,prefill_cursor;
 uint64_t prefill_received_tick[M16_FR_FRAMES];
} m16_frame_front_stream;
#ifdef __cplusplus
extern "C" {
#endif
/* 复用既有DMA驱动的2048模式，禁止接40960的正式BIT。
 * step由主循环调用；IRQ仍只进入m16_window_capture_irq。 */
int m16_frame_front_stream_init(m16_frame_front_stream *,m16_window_capture *,m16_front_ring *);
/* 单次模式只在新对象的停止态设置；采满20帧即停输入，明确不代表持续实时。 */
int m16_frame_front_stream_set_capture_once(m16_frame_front_stream *,unsigned enabled);
/* 只放行已获上下文存储额度的帧；堵塞不丢弃原始输入，过载由DMA明确报错。 */
int m16_frame_front_stream_limit(m16_frame_front_stream *,uint64_t frame_end);
int m16_frame_front_stream_start(m16_frame_front_stream *,unsigned epoch);
int m16_frame_front_stream_step(m16_frame_front_stream *);
int m16_frame_front_stream_stop(m16_frame_front_stream *);
int m16_frame_front_stream_releasable(const m16_frame_front_stream *);
int m16_frame_front_stream_restart(m16_frame_front_stream *,unsigned epoch);
#ifdef __cplusplus
}
#endif
#endif
