#ifndef MIC16_CANDIDATE_POOL_TASK_H
#define MIC16_CANDIDATE_POOL_TASK_H
#include "mic16_spatial.h"
#include <stdint.h>
enum {M16_CP_IDLE=0,M16_CP_VALIDATE_MAP=1,M16_CP_VALIDATE_LEVEL=2,M16_CP_VALIDATE_FLOOR=3,
 M16_CP_BEST=4,M16_CP_SECOND=5,M16_CP_VOTE=6,M16_CP_CLEAR=7,M16_CP_SEEDS=8,M16_CP_WEIGHTS=9,
 M16_CP_SUM=10,M16_CP_CENTER=11,M16_CP_CLUSTER=12,M16_CP_SORT=13,M16_CP_SELECT=14,
 M16_CP_COMPLETE=15,M16_CP_STOPPED=16,M16_CP_FAILED=17};
enum {M16_CP_INVALID=-700,M16_CP_BUSY=-701,M16_CP_INPUT=-702,M16_CP_FLOAT_ENV=-703};
typedef struct {
 unsigned magic,phase,cursor,row,best,seed_count,seed,weight_count,weight_index,frame_bits,band_bits;
 unsigned sort_index,sort_at,sort_loaded,selected_index,selected_peer,last_items,max_items;
 unsigned seeds[325],seen[325];
 int error;
 uint64_t steps;
 float second;
 double center[3],weight_sum;
 mic16_cluster sorting;
 const float *maps;
 const double *levels,*floor;
 double *votes;
 unsigned short *best_indices;
 mic16_pool *output;
 mic16_spatial_workspace *work;
} m16_candidate_pool_task;
#ifdef __cplusplus
extern "C" {
#endif
/* task、work首次为独立对象；work由调用方提供，不分配堆。 */
int m16_candidate_pool_init(m16_candidate_pool_task *,mic16_spatial_workspace *);
/* 固定完整[20,24,325]空间响应至480票及最多16个候选，输入/输出及工作区互不重叠。
 * 保持首峰/次峰、无门控投票、NumPy权重归约、稳定排序和10度候选去重。
 * 所有输出仅COMPLETE有效；不输出神经分数或声源有效判定。 */
int m16_candidate_pool_begin(m16_candidate_pool_task *,const float *,unsigned,const double *,unsigned,const double *,
 double *,unsigned,unsigned short *,mic16_pool *);
/* 每步64项检查/峰扫描，16行聚类，16次插入移动，4个角误差，
 * 或最多480权重的原树形归约。有限工作量不等于实际ARM耗时。 */
int m16_candidate_pool_step(m16_candidate_pool_task *);
int m16_candidate_pool_stop(m16_candidate_pool_task *);
int m16_candidate_pool_releasable(const m16_candidate_pool_task *);
#ifdef __cplusplus
}
#endif
#endif
