#ifndef MIC16_PIPELINE_H
#define MIC16_PIPELINE_H
#include "mic16_extended.h"
#include "mic16_spatial.h"
#include "mic16_frozen.h"
#include "mic16_pipeline_profile.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    mic16_extended_workspace extended;
    mic16_spatial_workspace spatial;
    mic16_score_workspace score;
    float maps[M16_SP_MAP_FLOATS];
    double votes[480*9],physical_directions[32];
    unsigned short best_indices[480];
    mic16_pool pool;
} mic16_pipeline_workspace;
typedef struct {
    float features[16*20*130],logits[16];
    double directions[32],scores[16];
    unsigned char valid[16];
    unsigned int candidate_count;
} mic16_pipeline_observation;
/* 归一化PCM[40960,16]与独立参考[8192,16]自动生成480条原始投票、C16候选、130维特征和冻结分数；本层不含状态记忆、IO或DMA。调用方拥有互不重叠的DDR输入、输出及工作区，不使用堆。顺序阶段复用频谱/PHAT存储，但特征模块仍重算频谱，不宣称计算复用。参数或PCM合同不符时不改输出；后续数值失败可能留下部分工作数据，不得使用。返回0成功，-1输入，-2参考，-3频谱，-4投影/投票/候选，-5特征，-6评分失败。 */
int mic16_pipeline_observe(const float *pcm,const float *reference,
    mic16_pipeline_observation *output,mic16_pipeline_workspace *workspace);
/* 计时版保持同一计算顺序。clock和profile必须同时提供，或同时为空。
 * profile与输入/输出/工作区/clock不可重叠。时钟倒退只使统计无效，
 * 不改算法结果；底层失败则保留原错误码和已尝试/完成阶段掩码。 */
int mic16_pipeline_observe_timed(const float *pcm,const float *reference,
    mic16_pipeline_observation *output,mic16_pipeline_workspace *workspace,
    const mic16_profile_clock *clock,mic16_pipeline_profile *profile);
/* 显式选择单窗公共谱、候选波束和通道对复用；仍使用原门限和网络。 */
int mic16_pipeline_observe_reused_timed(const float *,const float *,mic16_pipeline_observation *,
    mic16_pipeline_workspace *,const mic16_profile_clock *,mic16_pipeline_profile *);
int mic16_pipeline_observe_reused(const float *,const float *,mic16_pipeline_observation *,
    mic16_pipeline_workspace *);
#ifdef __cplusplus
}
#endif
#endif
