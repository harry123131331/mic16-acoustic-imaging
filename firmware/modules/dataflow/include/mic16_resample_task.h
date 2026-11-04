#ifndef MIC16_RESAMPLE_TASK_H
#define MIC16_RESAMPLE_TASK_H
#include <stdint.h>
#include "mic16_fir_provider.h"
enum {M16_RS_IDLE=0,M16_RS_VALIDATE=1,M16_RS_FILTER=2,M16_RS_COMPLETE=3,M16_RS_STOPPED=4,M16_RS_FAILED=5,M16_RS_DISPATCH=6,M16_RS_DEVICE=7};
enum {M16_RS_INVALID=-820,M16_RS_BUSY=-821,M16_RS_INPUT=-822,M16_RS_OUTPUT=-823,M16_RS_FLOAT_ENV=-824};
typedef struct {
 unsigned magic,phase,kind,samples,stride,channel,length,cursor,last_items,max_items;
 int error;
 uint64_t steps;
 const float *input32;
 const double *input64;
 double *output;
 const m16_fir_provider *provider;
} m16_resample_task;
#ifdef __cplusplus
extern "C" {
#endif
int m16_resample_task_init(m16_resample_task *);
/* 只在空闲终态首次绑定；原FIR保留为未绑定与数值范围不支持时的精确路径。 */
int m16_resample_task_bind(m16_resample_task *,const m16_fir_provider *);
/* 原256/625 FIR：F32支持1～40960点、1～16通道；F64供加权波束与连续重建，最多45680点。
 * 输出ceil(samples*256/625)个F64，输入/输出/task互不重叠，保持原抽头和旧输入优先累加顺序。 */
int m16_resample_task_begin32(m16_resample_task *,const float *,unsigned,unsigned,unsigned,double *,unsigned);
int m16_resample_task_begin64(m16_resample_task *,const double *,unsigned,double *,unsigned);
/* 每步64个输入检查，或一个输出点的最多49个非零抽头。输出仅COMPLETE有效。 */
int m16_resample_task_step(m16_resample_task *);
int m16_resample_task_stop(m16_resample_task *);
int m16_resample_task_releasable(const m16_resample_task *);
#ifdef __cplusplus
}
#endif
#endif
