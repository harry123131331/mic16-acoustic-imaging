#ifndef MIC16_ENH_MASK_TASK_H
#define MIC16_ENH_MASK_TASK_H
#include "mic16_enhancement.h"
#include <stdint.h>
enum {M16_EM_IDLE=0,M16_EM_VALIDATE=1,M16_EM_PROJECT=2,M16_EM_DEPTH=3,M16_EM_POINT=4,
 M16_EM_OUTPUT=5,M16_EM_CHECK=6,M16_EM_COMPLETE=7,M16_EM_STOPPED=8,M16_EM_FAILED=9};
enum {M16_EM_INVALID=-800,M16_EM_BUSY=-801,M16_EM_INPUT=-802,M16_EM_OUTPUT_ERROR=-803,M16_EM_FLOAT_ENV=-804};
typedef struct {
 unsigned magic,phase,cursor,layer,active,last_items,max_items;
 int error;
 uint64_t steps;
 const float *features;
 float *mask;
 m16_enh_network_workspace *work;
} m16_enh_mask_task;
#ifdef __cplusplus
extern "C" {
#endif
int m16_enh_mask_task_init(m16_enh_mask_task *,m16_enh_network_workspace *);
/* 固定67×64×10特征至67×64掩码，保持原FP16往返、冻结FP32网络和原1+.4*(base-1)。
 * 所有数组/task互不重叠；不使用新权重，不减少源数或频带。 */
int m16_enh_mask_task_begin(m16_enh_mask_task *,const float *,unsigned,float *,unsigned);
/* 每次64项校验或8个神经元，最多192次逐项乘加；输出只在COMPLETE有效。 */
int m16_enh_mask_task_step(m16_enh_mask_task *);
int m16_enh_mask_task_stop(m16_enh_mask_task *);
int m16_enh_mask_task_releasable(const m16_enh_mask_task *);
#ifdef __cplusplus
}
#endif
#endif
