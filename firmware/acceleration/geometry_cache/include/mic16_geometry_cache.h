#ifndef MIC16_GEOMETRY_CACHE_H
#define MIC16_GEOMETRY_CACHE_H
#include <stdint.h>
#define M16_GC_LAGS 4083u
#define M16_GC_BINS 807u
#define M16_GC_WORDS (M16_GC_LAGS*M16_GC_BINS)
enum {M16_GC_IDLE=0,M16_GC_INDEX=1,M16_GC_BUILD=2,M16_GC_READY=3,M16_GC_FAILED=4};
enum {M16_GC_INVALID=-1480,M16_GC_BUSY=-1481,M16_GC_ENVIRONMENT=-1482,M16_GC_GEOMETRY=-1483};
typedef struct {
 unsigned magic,phase,cursor,lag,bin,indexed,last_items,max_items;
 int error;
 uint64_t built_pairs,lookup_calls,steps;
 uint64_t *values;
 uint16_t representatives[M16_GC_LAGS];
} m16_geometry_cache;
#ifdef __cplusplus
extern "C" {
#endif
/* 上电一次生成冻结几何表，使用NOLOAD工作区，不将26MB常量塞进JTAG下载。
 * 只按已有精确F32滞后位模式共享，不近似合并坐标；包含负零符号恢复。
 * 首次task为零对象，values至少WORDS个64位字，必须独立、8字节对齐。
 * READY后只读表跨START/STOP保留；阵列几何变化必须重建固件，不复用旧表。 */
int m16_geometry_cache_init(m16_geometry_cache *,uint64_t *,unsigned words);
/* 每步最多64个索引或16个复系数，1进行中、0已准备、负数故障。 */
int m16_geometry_cache_step(m16_geometry_cache *);
/* 可直接绑定物理投影节点；未READY或浮点环境不符时拒绝，不静默回退慢路径。 */
int m16_geometry_cache_lookup(void *context,unsigned grid,unsigned pair,unsigned bin,float *real_imaginary);
/* 返回频点32～838的只读复数位行及虚部符号异或掩码；READY后一直有效。
 * 调用者只读，不得将行指针交给DMA写入。 */
int m16_geometry_cache_row(void *,unsigned grid,unsigned pair,const uint64_t **row,uint64_t *sign_mask);
#ifdef __cplusplus
}
#endif
#endif
