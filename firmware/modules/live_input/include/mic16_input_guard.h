#ifndef MIC16_INPUT_GUARD_H
#define MIC16_INPUT_GUARD_H
#include "mic16_live_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {
    M16_INPUT_OK=0,M16_INPUT_UNCHECKED=1,
    M16_INPUT_EXACT_ZERO=-40,M16_INPUT_INVALID_PCM=-41,
    M16_INPUT_INVALID_REFERENCE=-42,M16_INPUT_ARGUMENT=-43
};
typedef struct {
    int status;
    /* PCM未完整检查时zero_channel_mask为0xffffffff，不伪报16路全零。 */
    unsigned observed_checked,reference_checked,nonzero_observed,zero_channel_mask;
    unsigned invalid_index,invalid_is_reference;
} mic16_input_report;
/* 固定归一化FP32输入：PCM40960×16，独立参考8192×16。精确全零观测返回故障，
 * 不是正常无源；±0均为零，任何非零有限位型（包括非正规小量）均不因幅度低而拒绝。
 * 参考可全零，但仍须有限且在[-1,1]内。本层不能证明参考安静或物理采集正常。
 * 参数/存储别名错误时不写report；数值检查结果写入独立report。不申请堆、不改输入。 */
int mic16_input_inspect(const float *,const float *,mic16_input_report *);
typedef struct {
    mic16_pipeline_observer downstream;
    void *downstream_context;
    mic16_input_report report;
} mic16_guarded_observer_context;
/* 用于现有live bridge的同步回调。仅输入通过后才调用原检测回调。
 * context须与输入、输出及工作区分离。拒绝时不改观测/工作区，调用者必须检查返回值，
 * 丢弃旧观测/事件；live bridge负责停止当前时段并清记忆，应用再使参考失效。 */
int mic16_observe_with_input_guard(const float *,const float *,mic16_pipeline_observation *,
    mic16_pipeline_workspace *,void *);
#ifdef __cplusplus
}
#endif
#endif
