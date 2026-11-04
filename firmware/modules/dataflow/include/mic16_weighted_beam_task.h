#ifndef MIC16_WEIGHTED_BEAM_TASK_H
#define MIC16_WEIGHTED_BEAM_TASK_H
#include <stdint.h>
enum {M16_WB_IDLE=0,M16_WB_VALIDATE_PCM=1,M16_WB_VALIDATE_WEIGHTS=2,M16_WB_FILTER=3,
 M16_WB_COMPLETE=4,M16_WB_STOPPED=5,M16_WB_FAILED=6};
enum {M16_WB_INVALID=-840,M16_WB_BUSY=-841,M16_WB_INPUT=-842,M16_WB_OUTPUT=-843,M16_WB_FLOAT_ENV=-844};
typedef struct {
 unsigned magic,phase,cursor,channel,last_items,max_items;
 int error,delay[16];
 uint64_t steps;
 float coefficient[16][6];
 double total;
 const float *pcm,*weights;
 double *output;
} m16_weighted_beam_task;
#ifdef __cplusplus
extern "C" {
#endif
int m16_weighted_beam_init(m16_weighted_beam_task *);
/* 原增强加权波束，不是定位波束。pcm[40960,16]、weights[20,16]、direction[2]、
 * output[40960]及task分离。方向只在begin读取，其余输入至终态保持只读。 */
int m16_weighted_beam_begin(m16_weighted_beam_task *,const float *,const float *,const double *,double *);
/* 每步64个校验值或8通道×6抽头；保持原通道/抽头累加顺序，输出仅COMPLETE有效。 */
int m16_weighted_beam_step(m16_weighted_beam_task *);
int m16_weighted_beam_stop(m16_weighted_beam_task *);
int m16_weighted_beam_releasable(const m16_weighted_beam_task *);
#ifdef __cplusplus
}
#endif
#endif
