#ifndef MIC16_ENH_PREDICTION_H
#define MIC16_ENH_PREDICTION_H
#include "mic16_enhancement.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 掩码为67×64，调用方在返回前复制。这里只预测参数，最终音频由连续重建生成。
 * 回调失败则整窗预测作废；返回成功后，权重从原工作区weights[20×16]复制。
 * 保留原候选竞争、当前确认语义和冻结后滤波，不接受保持轨迹代替当前确认。 */
typedef int (*m16_enh_prediction_sink)(void *context,unsigned source_id,
    unsigned candidate_index,const float *mask);
int m16_enh_predict_window(const float *pcm,const float *quiet,
    const double *directions,const unsigned char *valid,
    const mic16_cycle_output *tracks,mic16_extended_workspace *shared,
    m16_enh_workspace *workspace,m16_enh_prediction_sink sink,void *context);
#ifdef __cplusplus
}
#endif
#endif
