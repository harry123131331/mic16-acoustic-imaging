#ifndef MIC16_CANDIDATE_BEAM_TASK_H
#define MIC16_CANDIDATE_BEAM_TASK_H
#include <stdint.h>
enum {M16_BT_IDLE=0,M16_BT_VALIDATE=1,M16_BT_PRODUCE=2,M16_BT_COMPLETE=3,M16_BT_STOPPED=4,M16_BT_FAILED=5};
enum {M16_BT_INVALID=-580,M16_BT_BUSY=-581,M16_BT_INPUT=-582,M16_BT_FLOAT_ENV=-583};
typedef struct {
 unsigned magic,phase,samples,cursor,guard,indexed,last_items,max_items;
 int error,offsets[16];
 uint64_t steps;
 const float *pcm;
 float *beam;
 double shifts[16],fractions[16];
} m16_candidate_beam_task;
/* 首次对象为零；输入[n,16]、输出[n]、task分离，n为8192或40960。
 * 原FP64插值后转FP32，以及16路树形求和顺序保持。无堆、无全窗同步调用。
 * 所有输入直到终态必须保持不变；COMPLETE以前输出无效。 */
int m16_candidate_beam_init(m16_candidate_beam_task *);
int m16_candidate_beam_begin(m16_candidate_beam_task *,const float *,unsigned pcm_values,unsigned samples,
 const double *direction,float *beam,unsigned beam_values);
/* 1进行中，0完成，2停止，负数失败。每步最多验证64个输入或计算4点×16路插值。 */
int m16_candidate_beam_step(m16_candidate_beam_task *);
int m16_candidate_beam_stop(m16_candidate_beam_task *);
int m16_candidate_beam_releasable(const m16_candidate_beam_task *);
#endif
