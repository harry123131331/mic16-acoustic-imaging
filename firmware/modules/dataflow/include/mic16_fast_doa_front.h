#ifndef MIC16_FAST_DOA_FRONT_H
#define MIC16_FAST_DOA_FRONT_H
#include "mic16_fast_doa_task.h"
#include "mic16_front_ring.h"
typedef struct {
 m16_front_ring *ring;
 m16_fast_doa_task *task;
 uint64_t copied_frames,cache_waits,output_waits,overwritten;
} m16_fast_doa_front;
#ifdef __cplusplus
extern "C" {
#endif
/* 在协调器commit之后由同一PS主循环调用；只取已提交的对应频谱和原始粗扫图。 */
int m16_fast_doa_front_step(m16_fast_doa_front *,uint64_t tick);
#ifdef __cplusplus
}
#endif
#endif
