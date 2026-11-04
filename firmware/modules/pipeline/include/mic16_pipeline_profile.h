#ifndef MIC16_PIPELINE_PROFILE_H
#define MIC16_PIPELINE_PROFILE_H

/* 只记录计算阶段耗时，不作为音频采样时间戳，不参与检测决策。
 * 时钟回调只能读取单调计数器，禁止修改输入、工作区或模型状态。 */
enum {
    MIC16_PROFILE_REFERENCE=0,
    MIC16_PROFILE_SPECTRAL,
    MIC16_PROFILE_SPATIAL,
    MIC16_PROFILE_POOL,
    MIC16_PROFILE_FEATURES,
    MIC16_PROFILE_NEURAL,
    MIC16_PROFILE_STAGES
};
typedef struct {
    void *context;
    unsigned long long (*read)(void *context);
} mic16_profile_clock;

typedef struct {
    unsigned long long ticks[MIC16_PROFILE_STAGES];
    unsigned long long last_tick;
    unsigned int reads,attempted_mask,completed_mask,clock_ok;
    int pipeline_rc;
} mic16_pipeline_profile;

#endif
