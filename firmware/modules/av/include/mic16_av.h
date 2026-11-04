#ifndef MIC16_AV_H
#define MIC16_AV_H
#include <stdint.h>
#include "mic16_frozen.h"
#define M16_AV_POINTS 8
#define M16_AV_MAGIC 0x4d415631u
#define M16_AV_VERSION 0x00010000u
#define M16_AV_POINT_VALID (1u<<16)
#define M16_AV_POINT_CURRENT (1u<<17)
/* 寄存器：0x00只读ID，04版本，08忙，0c只写提交；10标志、14剩余毫秒、18周期ID、1c标定ID；40+8*i为XY=(y<<16)|x，44+8*i为16位ID及有效/当前位。flags位0启用、位1已标定。写提交值1快照全部影子字段；同一时刻只允许一次提交，忙、无效有效期或标志会返回SLVERR。 */
enum { M16_AV_OK=0, M16_AV_INVALID=-1, M16_AV_BEHIND=-2,
       M16_AV_OUTSIDE=-3, M16_AV_NEEDS_RANGE=-4, M16_AV_STALE=-5 };
typedef struct {
    uint32_t calibrated, calibration_id, mount_id, video_mode_id;
    uint32_t width, height;
    double fx, fy, cx, cy, distortion[5], rotation[9], translation_m[3];
    uint32_t allow_direction_only;
} m16_av_calibration;
typedef struct { double x,y; uint32_t approximate; } m16_av_pixel;
typedef struct { uint32_t xy, attr; } m16_av_point;
typedef struct {
    uint32_t flags, remaining_ms, cycle_id, calibration_id;
    m16_av_point points[M16_AV_POINTS];
} m16_av_frame;
/* 不假设标定、距离、镜像或安装姿态。零距离仅请求显式许可的纯旋转近似；正距离为到阵列原点的欧氏距离，单位米。K/D对应实际显示帧，不能隐含裁剪或翻转。 */
int m16_av_validate(const m16_av_calibration *c);
int m16_av_project(const m16_av_calibration *c,double az,double el,
                  double range_m,m16_av_pixel *pixel);
/* 接收原冻结周期输出的元数据；未知距离使用方向模式，出错清空整帧。audio_end_ms与now_ms必须同一单调时钟，不能混用样本序号与RTC。保持轨迹保留源ID，仅当前确认记录绘实心点。 */
int m16_av_build(const m16_av_calibration *c,const mic16_cycle_output *cycle,
                uint32_t mount_id,uint32_t video_mode_id,
                uint64_t audio_end_ms,uint64_t now_ms,uint32_t max_age_ms,
                m16_av_frame *frame,uint32_t *omitted);
typedef uint32_t (*m16_av_read32)(void *context,uint32_t offset);
typedef void (*m16_av_write32)(void *context,uint32_t offset,uint32_t value);
/* 调用方提供有序MMIO操作，在ARM上使用Xil_In32/Xil_Out32及设备映射/屏障，单生产者。返回0为已接受写入序列，1忙，-1帧、设备或就绪条件错误。AXI SLVERR由平台处理；本函数不证明真实显示或标定准确。 */
int m16_av_submit(const m16_av_frame *frame,void *context,
                  m16_av_read32 read32,m16_av_write32 write32);
#endif
