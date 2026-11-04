#ifndef MIC16_FRONT_CONTEXT_H
#define MIC16_FRONT_CONTEXT_H
#include "mic16_front_ring.h"
#include "mic16_detection_task.h"
enum {M16_FC_IDLE=0,M16_FC_COPY=1,M16_FC_READY=2,M16_FC_IN_USE=3,M16_FC_STOPPING=4};
enum {M16_FC_INVALID=-1540,M16_FC_BUSY=-1541,M16_FC_LEASE=-1542,M16_FC_SEQUENCE=-1543};
/* 只将同一20帧上下文搬到既有后端布局，不重新计算历史FFT/PHAT。
 * pcm由平台分配；输出写租约由detector.front_context登记，防止正常begin抢写。
 * READY以后环读租约已归还；后端持有自己的PCM/PHAT，允许前端继续采集。
 * 消费端必须等检测及任何后续PCM读者（如全部源增强）结束，才显式retire。 */
typedef struct m16_front_context_task {
 unsigned magic,phase,frame,region,cursor,last_bytes,max_bytes,reader,submitted;
 int error;
 uint64_t epoch,first_sample,generation,bytes_copied,steps;
 m16_front_context_lease lease;
 m16_front_ring *ring;
 m16_detection_task *detector;
 /* 检测完成不等于所有PCM读者完成；父音频图登记额外归还条件。 */
 const void *downstream_owner;
 int (*downstream_releasable)(const void *);
 float *pcm;
} m16_front_context_task;
#ifdef __cplusplus
extern "C" {
#endif
int m16_front_context_init(m16_front_context_task *,m16_front_ring *,m16_detection_task *,float *,unsigned pcm_values);
/* 当前阶段只接受原40960对齐上下文；不提前打开hop4。 */
int m16_front_context_begin(m16_front_context_task *);
/* 每步最多复制1KiB，无设备访问、无浮点运算；0表示READY。 */
int m16_front_context_step(m16_front_context_task *);
int m16_front_context_retire(m16_front_context_task *);
int m16_front_context_stop(m16_front_context_task *);
#ifdef __cplusplus
}
#endif
#endif
