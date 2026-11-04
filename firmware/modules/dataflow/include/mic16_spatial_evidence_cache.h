#ifndef MIC16_SPATIAL_EVIDENCE_CACHE_H
#define MIC16_SPATIAL_EVIDENCE_CACHE_H
#include "mic16_coarse_scan_runtime.h"
#include "mic16_physical_projection_task.h"
enum {M16_EC_IDLE=0,M16_EC_RESERVED=1,M16_EC_CLEAR=2,M16_EC_WAIT=3,M16_EC_STOPPING=4,M16_EC_FAILED=5};
enum {M16_EC_INVALID=-1600,M16_EC_BUSY=-1601,M16_EC_SEQUENCE=-1602,M16_EC_INPUT=-1603,M16_EC_VALUE=-1604};
/* 原始粗扫图与精算值必须分开；原20帧联合选择决定哪些精算值进入本次输出图。 */
typedef struct {
 uint64_t epoch,frame;
 unsigned ready;
 float coarse[24u*325u],exact[24u*325u];
 unsigned char exact_valid[24u][41u];
} m16_spatial_evidence_slot;
typedef struct {m16_spatial_evidence_slot slots[20];} m16_spatial_evidence_storage;
typedef struct {
 unsigned magic,phase,slot,cursor,owned;
 int error;
 uint64_t epoch,next_frame,coarse_frames,exact_hits,exact_misses,exact_stores,evicted_stores;
 const float *phat;
 m16_coarse_scan_runtime *coarse;
 m16_spatial_evidence_storage *storage;
 m16_pp_grid_cache view;
} m16_spatial_evidence_cache;
#ifdef __cplusplus
extern "C" {
#endif
int m16_spatial_evidence_init(m16_spatial_evidence_cache *,m16_spatial_evidence_storage *,m16_coarse_scan_runtime *);
int m16_spatial_evidence_start(m16_spatial_evidence_cache *,uint64_t epoch);
/* 新帧开始写入前先使旧槽失效。以epoch/绝对帧号识别内容，不能以指针猜测输入相同。 */
int m16_spatial_evidence_reserve(m16_spatial_evidence_cache *,uint64_t epoch,uint64_t frame);
/* PHAT借用到本帧粗扫排空；只计算一次24带325方向，未完成的槽禁止快照。 */
int m16_spatial_evidence_begin(m16_spatial_evidence_cache *,const float *,unsigned values);
int m16_spatial_evidence_step(m16_spatial_evidence_cache *);
int m16_spatial_evidence_stop(m16_spatial_evidence_cache *);
int m16_spatial_evidence_releasable(const m16_spatial_evidence_cache *);
int m16_spatial_evidence_coarse(const m16_spatial_evidence_cache *,uint64_t epoch,uint64_t frame,const float **);
#ifdef __cplusplus
}
#endif
#endif
