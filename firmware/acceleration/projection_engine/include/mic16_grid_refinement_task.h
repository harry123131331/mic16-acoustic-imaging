#ifndef MIC16_GRID_REFINEMENT_TASK_H
#define MIC16_GRID_REFINEMENT_TASK_H
#include "mic16_physical_projection_task.h"
#include "mic16_scanner_hls.h"
enum {M16_RG_IDLE=0,M16_RG_VALIDATE_PHAT=1,M16_RG_VALIDATE_MAPS=2,M16_RG_CLEAR=3,
 M16_RG_FIRST=4,M16_RG_SECOND=5,M16_RG_SELECT=6,M16_RG_LIST=7,M16_RG_LAUNCH=8,
 M16_RG_DEVICE=9,M16_RG_CHECK_FIRST=10,M16_RG_CHECK_SECOND=11,M16_RG_CHECK_OUTSIDE=12,
 M16_RG_ZERO=13,M16_RG_COMPLETE=14,M16_RG_STOPPED=15,M16_RG_FAILED=16};
enum {M16_RG_INVALID=-440,M16_RG_BUSY=-441,M16_RG_INPUT=-442,M16_RG_OWNERSHIP=-443,M16_RG_TAGS=-444};
/* 初选与补算最多分成22块/频带，每块仍预留168个底层标签。 */
#define M16_RG_MAX_BATCHES (24u*22u)
#define M16_RG_TAGS (M16_RG_MAX_BATCHES*168u)
typedef struct {
 unsigned magic,phase,zone,frame,cursor,first,second,next_grid,batch_count,full,zero,stop_requested;
 unsigned batch[16],batches,last_items,max_items;
 unsigned char selected[325],computed[325];
 int error;
 uint64_t epoch,first_tag,reserved_tag_high,steps;
 const float *phat;
 float *maps;
 m16_scanner_refinement report;
 m16_physical_projection_task projection;
} m16_grid_refinement_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 节点、runtime/arena、输入PHAT与maps须各自独占且互不重叠。
 * maps必须是本窗既有Q2.24粗扫结果；本节点不生成/替代粗扫。
 * 阈值、15度第二峰规则、FP32保护带及全带回退与保留实现相同。 */
int m16_grid_refinement_init(m16_grid_refinement_task *,m16_projection_runtime *);
int m16_grid_refinement_begin(m16_grid_refinement_task *,const float *,unsigned phat_values,
    float *maps,unsigned map_values,uint64_t epoch,uint64_t first_tag);
/* 1进行中，0整窗精算完成，2停止，负数错误。每次最多64个网格/数据项，
 * 或一次底层分步操作；不是原整窗同步refine的外层线程包装。 */
/* 缓存只替代已计算过的同一帧/频带/网格；仍保留本上下文原联合选择、检查及回退。 */
int m16_grid_refinement_begin_cached(m16_grid_refinement_task *,const float *,unsigned,float *,unsigned,
 uint64_t epoch,uint64_t first_tag,m16_pp_grid_cache *,uint64_t first_frame);
int m16_grid_refinement_step(m16_grid_refinement_task *);
/* 1安全停止，0还需step排空设备，负数故障。部分maps不得用于投票/显示。 */
int m16_grid_refinement_stop(m16_grid_refinement_task *);
#ifdef __cplusplus
}
#endif
#endif
