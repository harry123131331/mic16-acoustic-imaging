#ifndef MIC16_AV_POLICY_H
#define MIC16_AV_POLICY_H
#include "mic16_av.h"
#define M16_VIDEO_DDR_BEGIN 0x10000000u
#define M16_VIDEO_DDR_END 0x10240000u
#define M16_VIDEO_STRIDE 0x000c0000u
#define M16_VIDEO_FRAME_BYTES (800u*480u*2u)
#define M16_AV_CONTROL_BASE 0x43c30000u
/* 当前简单DMA只使用64字节对齐、CPU静态分配的DDR缓冲区。 */
int m16_av_audio_dma_destination(uint32_t address,uint32_t bytes);
/* 在MMIO写入前调用。结果时间与当前时间必须来自同一单调毫秒时钟，不能用计算完成时刻替代音频采集时刻。无效、过期或时间未验证时清空标记。忙返回代表未提交，重试时必须重建结果。 */
int m16_av_publish_checked(const m16_av_calibration *,const mic16_cycle_output *,
    uint32_t mount,uint32_t mode,uint32_t timestamp_verified,
    uint64_t audio_end_ms,uint64_t now_ms,uint32_t max_age_ms,
    void *ctx,m16_av_read32,m16_av_write32,int *reason,uint32_t *omitted);
#endif
