#ifndef MIC16_ENH_PREDICTION_TASK_H
#define MIC16_ENH_PREDICTION_TASK_H
#include "mic16_channel_weights_task.h"
#include "mic16_weighted_beam_task.h"
#include "mic16_enh_spectrum_task.h"
#include "mic16_enh_feature_task.h"
#include "mic16_enh_mask_task.h"
#include "mic16_continuous.h"
enum {M16_EP_IDLE=0,M16_EP_VALIDATE_PCM=1,M16_EP_VALIDATE_REFERENCE=2,M16_EP_WEIGHTS=3,
 M16_EP_OBSERVED=4,M16_EP_REFERENCE=5,M16_EP_COPY_WEIGHTS=6,M16_EP_BEAM_BEGIN=7,M16_EP_BEAM=8,
 M16_EP_BEAM_SPECTRUM=9,M16_EP_FEATURE_BEGIN=10,M16_EP_FEATURE=11,M16_EP_MASK=12,M16_EP_COPY_MASK=13,
 M16_EP_COMPLETE=14,M16_EP_STOPPED=15,M16_EP_FAILED=16};
enum {M16_EP_INVALID=-900,M16_EP_BUSY=-901,M16_EP_INPUT=-902};
typedef struct {
 unsigned magic,phase,cursor,candidate,selected_count,source,selected[8],source_ids[8],last_items,max_items;
 int error,prediction_valid;
 uint64_t steps;
 double directions[32];
 unsigned char valid[16];
 const float *pcm,*reference;
 mic16_extended_workspace *shared;
 m16_enh_workspace *work;
 m16_cont_prediction *prediction;
 m16_channel_weights_task weights;
 m16_weighted_beam_task beam;
 m16_enh_spectrum_task spectrum;
 m16_enh_feature_task feature;
 m16_enh_mask_task mask;
} m16_enh_prediction_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 常驻初始化；shared与检测器分时借用，work及全部任务控制块在独立DDR区域。 */
int m16_enh_prediction_init(m16_enh_prediction_task *,mic16_extended_workspace *,m16_enh_workspace *);
/* 接受原始PCM/独立参考及已完成的C16轨迹，不重新判类。全部有效16候选参与增强竞争。
 * 仅当前确认、分数>=0.65、唯一ID和唯一候选输出；0～8源。方向/轨迹begin时复制，PCM/参考至终态只读。
 * 输出借用至终态，只有prediction_valid=1才可交给连续重建；本节点不虚构采样时间或端到端实时性。 */
int m16_enh_prediction_begin(m16_enh_prediction_task *,const float *,const float *,const double *,const unsigned char *,const mic16_cycle_output *,m16_cont_prediction *);
int m16_enh_prediction_step(m16_enh_prediction_task *);
int m16_enh_prediction_stop(m16_enh_prediction_task *);
int m16_enh_prediction_releasable(const m16_enh_prediction_task *);
#ifdef __cplusplus
}
#endif
#endif
