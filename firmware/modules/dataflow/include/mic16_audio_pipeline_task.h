#ifndef MIC16_AUDIO_PIPELINE_TASK_H
#define MIC16_AUDIO_PIPELINE_TASK_H
#include "mic16_detection_task.h"
#include "mic16_source_audio_task.h"
enum {M16_AP_IDLE=0,M16_AP_DETECT=1,M16_AP_SOURCE_AUDIO=2,M16_AP_DRAIN=3,
 M16_AP_COMPLETE=4,M16_AP_STOPPED=5,M16_AP_FAILED=6};
enum {M16_AP_INVALID=-960,M16_AP_BUSY=-961,M16_AP_INPUT=-962,M16_AP_SEQUENCE=-963};
typedef struct {
 unsigned magic,phase,stop_sent,finishing;
 int error;
 uint64_t steps,first_sample;
 const float *pcm,*reference;
 m16_detection_task *detection;
 struct m16_front_context_task *front_context;
 m16_source_audio_task *source_audio;
 mic16_pipeline_observation *observation;
} m16_audio_pipeline_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 完整原始PCM→检测→源ID→全部确认源增强→连续音频计算图。
 * 检测与增强顺序借用同一extended工作区；设备控制对象、输入/输出和音频历史各自独立。
 * source_audio已按空历史epoch初始化，硬件后端已完成常驻初始化。此处不采样、不直接发UDP。 */
int m16_audio_pipeline_init(m16_audio_pipeline_task *,m16_detection_task *,m16_source_audio_task *,mic16_pipeline_observation *);
/* 原始PCM40960×16、独立参考8192×16；first_sample必须连续，从epoch内0开始。
 * 内部分配从未使用过的FFT事务/共享核标签，取消后也不复用。输入借用至真正releasable。 */
int m16_audio_pipeline_begin(m16_audio_pipeline_task *,const float *,const float *,uint64_t first_sample);
/* 输入来自同一检测器的READY上下文；仍执行全部确认源增强和连续重建。 */
int m16_audio_pipeline_begin_prepared(m16_audio_pipeline_task *,struct m16_front_context_task *,const float *reference);
int m16_audio_pipeline_step(m16_audio_pipeline_task *);
int m16_audio_pipeline_location(const m16_audio_pipeline_task *,const mic16_cycle_output **,const mic16_pipeline_observation **);
int m16_audio_pipeline_location_release(m16_audio_pipeline_task *,uint32_t epoch,uint32_t window);
int m16_audio_pipeline_output(const m16_audio_pipeline_task *,const m16_cont_info **,const double **,const double **,unsigned *);
int m16_audio_pipeline_output_release(m16_audio_pipeline_task *,uint32_t epoch,uint32_t window,uint32_t source_id);
/* 正常结束流：采集停止、最后一窗计算完成后flush一窗前瞻历史；与紧急取消分开。 */
int m16_audio_pipeline_finish(m16_audio_pipeline_task *);
/* 请求取消后仍需step和硬件服务，直到真实租约排空；隔离故障不能被软件清状态掩盖。 */
int m16_audio_pipeline_stop(m16_audio_pipeline_task *);
int m16_audio_pipeline_releasable(const m16_audio_pipeline_task *);
int m16_audio_pipeline_reset(m16_audio_pipeline_task *,uint32_t new_epoch,const char *new_key);
#ifdef __cplusplus
}
#endif
#endif
