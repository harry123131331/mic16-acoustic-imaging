#ifndef MIC16_FRONT_PIPELINE_H
#define MIC16_FRONT_PIPELINE_H
#include "mic16_front_ring.h"

/* 此状态由新协调器独占；旧ring_step/旧DMA bridge不得同时推进同一ring。 */
#define M16_FR_PIPELINED 7u
enum {M16_FPL_NONE=0,M16_FPL_COPY=1,M16_FPL_FRONT=2,M16_FPL_READY=3};
typedef struct {
 unsigned magic,active,front_phase,pause_requested,cursor,front_slot;
 int error;
 uint64_t next_accept,front_frame,last_tick;
 uint64_t steps,overlap_steps,backpressure,committed,discarded_on_stop;
 m16_front_ring *ring;
} m16_front_pipeline;

#ifdef __cplusplus
extern "C" {
#endif
/* 显式拥有一个已绑定独立粗扫、处于停止态的原ring；不分配新历史数组。 */
int m16_front_pipeline_init(m16_front_pipeline *,m16_front_ring *);
int m16_front_pipeline_start(m16_front_pipeline *,uint64_t epoch,uint64_t tick);
int m16_front_pipeline_can_accept(const m16_front_pipeline *);
int m16_front_pipeline_begin(m16_front_pipeline *,const float *,unsigned values,uint64_t epoch,
 uint64_t frame,uint64_t first_sample,uint64_t received_tick);
/* 每次推进至多一个原粗扫片段及一个原前端片段；0表示当前排空，不是关闭采集。 */
int m16_front_pipeline_step(m16_front_pipeline *,uint64_t tick);
/* 暂停接收并排空后，原ring进入IDLE，才允许原20帧快照。 */
int m16_front_pipeline_pause(m16_front_pipeline *);
int m16_front_pipeline_resume(m16_front_pipeline *);
int m16_front_pipeline_stop(m16_front_pipeline *);
int m16_front_pipeline_releasable(const m16_front_pipeline *);
#ifdef __cplusplus
}
#endif
#endif
