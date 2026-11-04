#ifndef MIC16_PHYSICAL_FEATURE_TASK_H
#define MIC16_PHYSICAL_FEATURE_TASK_H
#include "mic16_candidate_acoustics_task.h"
#include "mic16_physical_projection_task.h"
enum {M16_PF_IDLE=0,M16_PF_PREPARED=1,M16_PF_VOTES=2,M16_PF_PHAT=3,M16_PF_LAUNCH=4,
 M16_PF_RUN=5,M16_PF_CHECK=6,M16_PF_DRAIN=7,M16_PF_COMPLETE=8,M16_PF_STOPPED=9,M16_PF_FAILED=10};
enum {M16_PF_ACOUSTICS=0,M16_PF_ANGLES=1,M16_PF_GATHER=2,M16_PF_LOCAL=3,M16_PF_COLLATE=4,M16_PF_OWN_DONE=5,M16_PF_BEAM_LEASE=6};
enum {M16_PF_INVALID=-620,M16_PF_BUSY=-621,M16_PF_INPUT=-622,M16_PF_OUTPUT=-623,M16_PF_FLOAT_ENV=-624};
typedef struct {
 unsigned magic,phase,own_phase,count,candidate,cursor,frame,local_count,frame_bits,band_bits;
 unsigned projection_begun,acoustics_begun,projection_done,projection_owned;
 unsigned projection_terminal,acoustics_terminal,projection_stop_sent,acoustics_stop_sent;
 unsigned last_items,max_items;
 unsigned beam_hold_enabled,beam_leases,beam_releases;
 int error;
 uint64_t epoch,first_job,first_tag,last_epoch,reserved_job_high,reserved_tag_high,steps,CPU_steps_while_projection;
 const float *pcm,*reference;
 const double *votes;
 float *features;
 double directions[32],coherence[24],contrast[24],angles[24];
 mic16_physical_workspace *work;
 m16_shared_spectrum_task *spectrum;
 m16_projection_runtime *runtime;
 m16_physical_projection_task projection;
 m16_candidate_acoustics_task acoustics;
} m16_physical_feature_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 首次零对象；两后端拥有独立arena，work.spectral已初始化。整个task/work及后端存储分离。 */
int m16_physical_feature_init(m16_physical_feature_task *,m16_shared_spectrum_task *,m16_projection_runtime *,mic16_physical_workspace *);
/* 完整[C,20,88]，C仍为1～16。公共PHAT、levels及reference须由prepared明确绑定原PCM。
 * 不重新计算公共前端或采集参考；从begin至终态由父调度器持有所有输入只读租约。
 * 预留2*C个FFT事务和168个空间核标签；取消也不重复使用已预留范围。
 * 全部88列只有COMPLETE有效；不是130维、评分、增强或整机完成标记。 */
int m16_physical_feature_begin(m16_physical_feature_task *,const float *,unsigned,const float *,unsigned,
 const double *,unsigned,const double *,unsigned,float *,unsigned,const mic16_prepared_spectral *,uint64_t,uint64_t,uint64_t);
/* 1继续、0完整88列、2安全停止、负数失败。RUN每次至多推进一次空间核和一次候选/归约节点。
 * CPU本层最多64项、角误差8次、局部投票4行，或一次58项拼接；子节点各自遵守原步长。
 * CPU和设备实际耗时仍需实板测量，工作项不是时钟周期或物理采样时刻。 */
int m16_physical_feature_step(m16_physical_feature_task *);
int m16_physical_feature_stop(m16_physical_feature_task *);
int m16_physical_feature_releasable(const m16_physical_feature_task *);
/* 仅空闲终态配置。启用后每个真实候选波束保留到父节点显式归还，避免统计支路重复构建。
 * 保留期间step仍推进空间后端；不会进入下一候选覆盖work.beam。
 * 父节点必须先停止/排空波束消费者，再允许本节点停止后复用工作区。 */
int m16_physical_feature_hold_beam(m16_physical_feature_task *,unsigned enabled);
const float *m16_physical_feature_beam(const m16_physical_feature_task *,uint64_t epoch,unsigned candidate);
int m16_physical_feature_release_beam(m16_physical_feature_task *,uint64_t epoch,unsigned candidate);
#ifdef __cplusplus
}
#endif
#endif
