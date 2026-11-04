#ifndef MIC16_STATISTICS_TILE_BATCH_H
#define MIC16_STATISTICS_TILE_BATCH_H
#include <stdint.h>
#include "mic16_extended.h"
/* 单一检测所有者；available必须在隔离/忙碌时返回0。execute须在硬件完全停止且
 * 结果可见后返回0。失败后不静默回退CPU；不从中断或协作回调递归调用本模块。
 * arena至少2304个64位字、64字节对齐；观测/系数/输出偏移为0/1024/2048。
 * observations_changed为0仅表示刚完成的同一观测块可复用，不允许CPU改写它。
 * 当前完整统计入口协作等待，尚不是跨窗异步流水或持续实时验收。 */
typedef struct {
    void *context;
    uint64_t *arena;
    int (*available)(void *);
    int (*execute)(void *,unsigned,unsigned,unsigned,unsigned);
} m16_tile_batch_backend;
typedef struct {unsigned calls,rows,rotation_groups,batches,backend_errors;} m16_tile_batch_report;
extern m16_tile_batch_report m16_tile_batch_counts;
int m16_tile_batch_bind(const m16_tile_batch_backend *);
int m16_tile_batch_compute(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
#endif
