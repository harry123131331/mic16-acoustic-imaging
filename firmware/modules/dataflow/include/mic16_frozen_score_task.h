#ifndef MIC16_FROZEN_SCORE_TASK_H
#define MIC16_FROZEN_SCORE_TASK_H
#include "mic16_frozen.h"
#include <stdint.h>
enum {M16_NT_IDLE=0,M16_NT_VALIDATE_X=1,M16_NT_VALIDATE_D=2,M16_NT_VALIDATE_V=3,M16_NT_CANDIDATE=4,
 M16_NT_PROJECT=5,M16_NT_TEMPORAL=6,M16_NT_COPY=7,M16_NT_POOL=8,M16_NT_BAND_STD=9,M16_NT_DIFF=10,
 M16_NT_ENTROPY=11,M16_NT_STATISTICS=12,M16_NT_NEXT=13,M16_NT_NEIGHBORS=14,M16_NT_CONTEXT_INIT=15,
 M16_NT_CONTEXT_ADD=16,M16_NT_CONTEXT_FINISH=17,M16_NT_HEAD64=18,M16_NT_HEAD32=19,M16_NT_HEAD1=20,
 M16_NT_PROBABILITY=21,M16_NT_COMPLETE=22,M16_NT_STOPPED=23,M16_NT_FAILED=24};
enum {M16_NT_INVALID=-720,M16_NT_BUSY=-721,M16_NT_INPUT=-722,M16_NT_OUTPUT=-723,M16_NT_FLOAT_ENV=-724};
typedef struct {
 unsigned magic,phase,candidate,cursor,layer,neighbor,neighbor_count,last_items,max_items;
 int error;
 uint64_t steps;
 float band_std,absdiff,entropy[20],flatness[20];
 double neighbor_limit;
 const float *features;
 const double *directions;
 const unsigned char *valid_candidates;
 float *logits;
 double *probabilities;
 mic16_score_workspace *work;
} m16_frozen_score_task;
#ifdef __cplusplus
extern "C" {
#endif
int m16_frozen_score_task_init(m16_frozen_score_task *,mic16_score_workspace *);
/* 固定[16,20,130]/32方向/16有效位，输出16 logits及16概率。所有缓冲区彼此分离。
 * 权重、加法次序、近邻上下文及无效候选-20保持原C16；不修改0.65判定口径。
 * 不在此节点训练、更新权重或维护源ID。输出只在COMPLETE有效。 */
int m16_frozen_score_task_begin(m16_frozen_score_task *,const float *,unsigned,const double *,
 const unsigned char *,float *,double *);
/* 每步最多一个投影/卷积/全连接神经元（最多215次乘加），或一帧32带统计，
 * 或一个候选的上下文归约片段；保留每个原表达式的精度与累加次序。
 * 这是有限CPU任务，不是神经网络HLS加速或实板时间保证。 */
int m16_frozen_score_task_step(m16_frozen_score_task *);
int m16_frozen_score_task_stop(m16_frozen_score_task *);
int m16_frozen_score_task_releasable(const m16_frozen_score_task *);
#ifdef __cplusplus
}
#endif
#endif
