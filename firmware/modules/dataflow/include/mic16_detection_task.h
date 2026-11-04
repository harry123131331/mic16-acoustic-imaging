#ifndef MIC16_DETECTION_TASK_H
#define MIC16_DETECTION_TASK_H
#include "mic16_pipeline.h"
#include "mic16_spatial_front_task.h"
#include "mic16_feature_pipeline_task.h"
#include "mic16_candidate_pool_task.h"
#include "mic16_frozen_score_task.h"
enum {M16_DT_IDLE=0,M16_DT_REFERENCE_BEGIN=1,M16_DT_REFERENCE=2,M16_DT_REFERENCE_ADD=3,M16_DT_SCAN=4,
 M16_DT_POOL=5,M16_DT_DIRECTIONS=6,M16_DT_FEATURES=7,M16_DT_SCORE=8,M16_DT_DRAIN=9,
 M16_DT_COMPLETE=10,M16_DT_STOPPED=11,M16_DT_FAILED=12};
enum {M16_DT_INVALID=-740,M16_DT_BUSY=-741,M16_DT_INPUT=-742,M16_DT_SEQUENCE=-743};
#define M16_DT_TAGS (M16_RG_TAGS+304u)
struct m16_front_context_task;
typedef struct {
 unsigned magic,phase,reference_frame,front_started,front_done,features_started,features_done;
 unsigned prepared_input;
 struct m16_front_context_task *front_context;
 unsigned front_terminal,features_terminal,front_stop_sent,features_stop_sent,has_run;
 int error;
 uint64_t epoch,first_sample,first_job,first_tag,last_epoch,last_sample,reserved_job_high,reserved_tag_high,steps;
 const float *pcm,*reference;
 mic16_pipeline_workspace *work;
 mic16_pipeline_observation *output;
 m16_coarse_scan_runtime *coarse;
 m16_shared_spectrum_task *spectrum;
 m16_projection_runtime *runtime;
 m16_statistics_projection_storage *statistics_storage;
 m16_spatial_front_task front;
 m16_candidate_pool_task pool;
 m16_feature_pipeline_task features;
 m16_frozen_score_task score;
} m16_detection_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 常驻启动时调用一次；公共谱表和粗扫相位表已完成初始化，三个设备物理arena分离。
 * task、完整pipeline工作区、statistics_storage及设备对象/arena彼此独立。 */
int m16_detection_init(m16_detection_task *,mic16_pipeline_workspace *,m16_coarse_scan_runtime *,
 m16_shared_spectrum_task *,m16_projection_runtime *,m16_statistics_projection_storage *);
/* 真正PCM至冻结16候选评分链，不接受预先构造的PHAT/候选作为替代输入。
 * PCM[40960,16]、提供的独立参考[8192,16]和output分别与task/work/设备分离。
 * first_sample是epoch内逻辑样本号且为40960整数倍，不能冒充已校准物理采样时间。
 * 每窗预留32个FFT事务及M16_DT_TAGS个共享核标签，取消也不复用。
 * 本任务不采背景、不进行声纹分类，也不删除定向噪声候选。
 * 所有输出仅COMPLETE有效；源ID记忆、全部源增强及网络发送由常驻父调度器联接。 */
int m16_detection_begin(m16_detection_task *,const float *,unsigned,const float *,unsigned,
 mic16_pipeline_observation *,uint64_t epoch,uint64_t first_sample,uint64_t first_job,uint64_t first_tag);
/* 只消费同一detector登记的READY上下文；不接受随意构造的外部PHAT。
 * 上下文存储保持到显式retire，包含原PCM后续增强的读租约。 */
int m16_detection_begin_prepared(m16_detection_task *,struct m16_front_context_task *,const float *,unsigned,
 mic16_pipeline_observation *,uint64_t first_job,uint64_t first_tag);
int m16_detection_prepared_releasable(const m16_detection_task *,const struct m16_front_context_task *);
/* 一次只推进一个真实子节点或24带参考小段归约，无旧整窗检测阻塞调用。 */
int m16_detection_step(m16_detection_task *);
int m16_detection_stop(m16_detection_task *);
int m16_detection_releasable(const m16_detection_task *);
#ifdef __cplusplus
}
#endif
#endif
