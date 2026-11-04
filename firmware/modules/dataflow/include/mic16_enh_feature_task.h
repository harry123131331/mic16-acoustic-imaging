#ifndef MIC16_ENH_FEATURE_TASK_H
#define MIC16_ENH_FEATURE_TASK_H
#include "mic16_extended.h"
#include <stdint.h>
enum {M16_EF_IDLE=0,M16_EF_SIGNATURE=1,M16_EF_CLEAR=2,M16_EF_PAIRS=3,M16_EF_BAND=4,M16_EF_OUTPUT=5,
 M16_EF_COMPLETE=6,M16_EF_STOPPED=7,M16_EF_FAILED=8};
enum {M16_EF_INVALID=-880,M16_EF_BUSY=-881,M16_EF_INPUT=-882,M16_EF_OUTPUT_ERROR=-883,M16_EF_FLOAT_ENV=-884};
typedef struct {
 unsigned magic,phase,candidate,frame,bin,pair,cursor,last_items,max_items;
 int error;
 uint64_t steps;
 double band[64][5],sr,si,lr,li;
 float arrivals[16];
 unsigned char valid[16];
 mic16_extended_workspace *work;
} m16_enh_feature_task;
#ifdef __cplusplus
extern "C" {
#endif
int m16_enh_feature_init(m16_enh_feature_task *,mic16_extended_workspace *);
/* 完整当前16候选参与竞争。输入方向已由冻结检测器完成FP32往返；只在begin读取。
 * work内FP64增强频谱、参考/通道/全部有效候选功率须已完成，处理期间由本任务独占。
 * 输出work.spectral10[67,64,10]仅COMPLETE有效，不替换冻结模型或阈值。 */
int m16_enh_feature_begin(m16_enh_feature_task *,const double *,const unsigned char *,unsigned candidate);
/* 每步8对相位、64项清零、8对复数乘加或单频带10维特征，原归约/对数顺序保持。 */
int m16_enh_feature_step(m16_enh_feature_task *);
int m16_enh_feature_stop(m16_enh_feature_task *);
int m16_enh_feature_releasable(const m16_enh_feature_task *);
#ifdef __cplusplus
}
#endif
#endif
