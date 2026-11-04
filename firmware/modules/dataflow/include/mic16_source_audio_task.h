#ifndef MIC16_SOURCE_AUDIO_TASK_H
#define MIC16_SOURCE_AUDIO_TASK_H
#include "mic16_pipeline.h"
#include "mic16_enh_prediction_task.h"
#include "mic16_continuous_task.h"
enum {M16_SA_IDLE=0,M16_SA_TRACK=1,M16_SA_PREDICT=2,M16_SA_REBUILD=3,M16_SA_WAIT_LOCATION=4,
 M16_SA_COMPLETE=5,M16_SA_FINISH=6,M16_SA_STOPPED=7,M16_SA_FAILED=8};
enum {M16_SA_INVALID=-940,M16_SA_BUSY=-941,M16_SA_INPUT=-942,M16_SA_SEQUENCE=-943};
typedef struct {
 unsigned magic,phase,epoch,closed,location_ready,last_was_finish;
 int error;
 /* 0保留完整历史链路，1只做原多源判决及RESULT交接。 */
 unsigned localization_only;
 uint64_t first_sample,next_sample,steps;
 const float *pcm,*reference,*epoch_reference;
 const mic16_pipeline_observation *observation;
 char pose_reference_key[512];
 mic16_search_memory memory;
 mic16_cycle_output tracks;
 m16_cont_prediction prediction;
 m16_enh_prediction_task *enhancement;
 m16_continuous_task *continuous;
} m16_source_audio_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 连接原源ID记忆、完整新预测父图及持续重建。控制对象含预测暂存，必须放DDR/BSS。
 * continuous已按非零epoch初始化且无历史，enhancement借用已经结束的检测工作区。
 * key绑定姿态/参考/音频来源，最长511字节，整个epoch保持不变；不自动采背景。 */
int m16_source_audio_init(m16_source_audio_task *,m16_enh_prediction_task *,m16_continuous_task *,const char *key);
/* observation必须来自已COMPLETE的检测任务；本节点不伪造或重算冻结分数。
 * PCM/参考/观测借用至本次COMPLETE；一个epoch内参考对象不替换，first_sample从0连续递增。
 * 定位在原记忆更新后立即就绪，继续并行推进增强；不等待前一窗音频才产生当前定位。 */
/* 只允许在新epoch尚未处理任何窗的空闲状态选择，禁止运行中偷换模式。 */
int m16_source_audio_set_localization_only(m16_source_audio_task *,unsigned enabled);
int m16_source_audio_begin(m16_source_audio_task *,const float *,const float *,const mic16_pipeline_observation *,uint64_t first_sample);
int m16_source_audio_step(m16_source_audio_task *);
int m16_source_audio_location(const m16_source_audio_task *,const mic16_cycle_output **,const mic16_pipeline_observation **);
int m16_source_audio_location_release(m16_source_audio_task *,uint32_t epoch,uint32_t window);
int m16_source_audio_output(const m16_source_audio_task *,const m16_cont_info **,const double **,const double **,unsigned *);
int m16_source_audio_output_release(m16_source_audio_task *,uint32_t epoch,uint32_t window,uint32_t source_id);
int m16_source_audio_finish(m16_source_audio_task *);
int m16_source_audio_stop(m16_source_audio_task *);
int m16_source_audio_releasable(const m16_source_audio_task *);
int m16_source_audio_reset(m16_source_audio_task *,uint32_t new_epoch,const char *new_key);
#ifdef __cplusplus
}
#endif
#endif
