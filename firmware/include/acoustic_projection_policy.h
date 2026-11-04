#ifndef ACOUSTIC_PROJECTION_POLICY_H
#define ACOUSTIC_PROJECTION_POLICY_H
#include "mic16_av.h"
/* 距离策略与镜头标定分开保存；未知距离不能自动替换成两米。 */
enum { ACOUSTIC_DIRECTION_ONLY=0, ACOUSTIC_MEASURED_RANGE=1, ACOUSTIC_KNOWN_PLANE=2 };
typedef struct {
    uint32_t policy_id, mode, assumption_acknowledged;
    /* 平面在阵列坐标中定义：normal·P=offset_m，法向量必须为单位向量。 */
    double normal[3], offset_m;
    uint32_t range_count, source_ids[M16_AV_POINTS];
    /* 每个源的欧氏距离，以源ID关联，不能依赖列表顺序。 */
    double ranges_m[M16_AV_POINTS];
} acoustic_projection_policy;
typedef struct {
    uint32_t policy_id, mode, approximate_points, omitted;
    int point_reason[M16_AV_POINTS];
    double used_range_m[M16_AV_POINTS];
} acoustic_projection_report;
int acoustic_build_overlay(const m16_av_calibration *, const acoustic_projection_policy *,
    const mic16_cycle_output *, uint32_t mount, uint32_t video_mode,
    uint64_t audio_end_ms, uint64_t now_ms, uint32_t max_age_ms,
    m16_av_frame *, acoustic_projection_report *);
/* 写寄存器前重新检查时间；失败时提交空标记，不延长旧结果寿命。 */
int acoustic_publish_overlay(const m16_av_calibration *, const acoustic_projection_policy *,
    const mic16_cycle_output *, uint32_t mount, uint32_t video_mode, uint32_t timestamp_verified,
    uint64_t audio_end_ms, uint64_t now_ms, uint32_t max_age_ms,
    void *, m16_av_read32, m16_av_write32, int *, acoustic_projection_report *);
#endif
