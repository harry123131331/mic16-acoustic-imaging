#ifndef MIC16_FEATURE_PIPELINE_TASK_H
#define MIC16_FEATURE_PIPELINE_TASK_H
#include "mic16_physical_feature_task.h"
#include "mic16_statistics_front_task.h"
#include "mic16_statistics_projection_task.h"
enum {M16_FP_IDLE=0,M16_FP_QUIET=1,M16_FP_OBSERVED=2,M16_FP_PHYSICAL=3,M16_FP_STATISTICS=4,
 M16_FP_MODEL=5,M16_FP_COLLATE106=6,M16_FP_UNITS=7,M16_FP_PEERS=8,M16_FP_CONTRAST=9,
 M16_FP_OUTPUT=10,M16_FP_DRAIN=11,M16_FP_COMPLETE=12,M16_FP_STOPPED=13,M16_FP_FAILED=14};
enum {M16_FP_INVALID=-680,M16_FP_BUSY=-681,M16_FP_INPUT=-682,M16_FP_OUTPUT_ERROR=-683,M16_FP_FLOAT_ENV=-684,M16_FP_OWNERSHIP=-685};
typedef struct {
 unsigned magic,phase,count,cursor,beam_running,beam_completed,physical_started,statistics_started;
 unsigned physical_terminal,statistics_terminal,physical_stop_sent,statistics_stop_sent,last_items,max_items;
 int error;
 uint64_t epoch,first_job,first_tag,last_epoch,reserved_job_high,reserved_tag_high,steps;
 const float *pcm,*reference;
 const double *votes;
 float *output;
 double directions[32],units[48];
 double *model_directions;
 unsigned char *valid_candidates;
 mic16_extended_workspace *work;
 m16_statistics_projection_storage *storage;
 m16_shared_spectrum_task *spectrum;
 m16_projection_runtime *runtime;
 m16_physical_feature_task physical;
 m16_statistics_front_task front;
 m16_statistics_projection_task statistics;
} m16_feature_pipeline_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 首次零对象；与work、storage及两个设备的对象/arena分离。公共prepared已完成。
 * 合并真实分步节点，未调用旧整窗特征函数。 */
int m16_feature_pipeline_init(m16_feature_pipeline_task *,m16_shared_spectrum_task *,m16_projection_runtime *,
 mic16_extended_workspace *,m16_statistics_projection_storage *);
/* 输入PCM/ref/投票/方向与work和所有输出分离。输出固定[16,20,130]，未用候选清零。
 * prepared须绑定同一PCM/ref及work.physical；父调度器持有输入至安全终态。
 * 预留2*C个FFT事务及304个空间标签，取消不复用。开始不会重新采集参考。
 * model_directions[32]和valid_candidates[16]也仅COMPLETE有效。
 * 统计会在88维安全释放PHAT后改写其union；不能再用旧prepared重复处理下一窗。 */
int m16_feature_pipeline_begin(m16_feature_pipeline_task *,const float *,unsigned,const float *,unsigned,
 const double *,unsigned,const double *,unsigned,float *,unsigned,double *,unsigned char *,
 const mic16_prepared_spectral *,uint64_t,uint64_t,uint64_t);
/* 每步至多推进一次物理节点和一次统计前处理节点，或一次统计后处理节点。
 * 汇合最多64项；方向单位向量一次1个、邻居角一次4个、对比一次4×16项。
 * 不提供ARM耗时保证，也不是采集/评分/增强/音视频常驻主循环。 */
int m16_feature_pipeline_step(m16_feature_pipeline_task *);
int m16_feature_pipeline_stop(m16_feature_pipeline_task *);
int m16_feature_pipeline_releasable(const m16_feature_pipeline_task *);
#ifdef __cplusplus
}
#endif
#endif
