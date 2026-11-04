#ifndef MIC16_LIVE_BRIDGE_H
#define MIC16_LIVE_BRIDGE_H
#include "mic16_live_input.h"
#include "mic16_pipeline.h"
typedef struct { uint64_t epoch; int has_epoch; } mic16_live_binding;
/* 同步单所有者接口，内部无采集、DMA或缓存操作。调用方令binding.has_epoch=0，并提供分离的工作区与输出。边界后的首个完整窗先清空源记忆，再调用原冻结状态规则；不对补零或缺口窗评分。结束秒数仅为当前时段的标称时间。返回0成功，其他为适配错误，-10流水线失败，-11状态失败；失败时停止采集并清空记忆，不显示事件。 */
int mic16_live_run(mic16_live_input *,mic16_live_binding *,mic16_search_memory *,
    mic16_pipeline_observation *,mic16_pipeline_workspace *,mic16_cycle_output *);
/* 给同一流水线增加计时适配；窗口借用、缺口、轨迹及释放规则保持一致。 */
typedef int (*mic16_pipeline_observer)(const float *,const float *,
    mic16_pipeline_observation *,mic16_pipeline_workspace *,void *);
int mic16_live_run_with_observer(mic16_live_input *,mic16_live_binding *,mic16_search_memory *,
    mic16_pipeline_observation *,mic16_pipeline_workspace *,mic16_cycle_output *,
    mic16_pipeline_observer,void *);
#endif
