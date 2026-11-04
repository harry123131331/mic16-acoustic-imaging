#ifndef MIC16_AV_RUNTIME_SITE_H
#define MIC16_AV_RUNTIME_SITE_H
/* 必须现场核验网卡/IP和安装编号后显式开启。禁止靠调试器改DDR绕过配置。 */
#define M16_PC_VIDEO_ENABLED 1u
#define M16_PC_VIDEO_PORT 5002u
#define M16_PC_VIDEO_EPOCH 20261021u
#define M16_PC_VIDEO_TRANSFORM_ID 1u /* 未旋转未镜像的编码格式版本，不代表真实声像标定。 */
/* 应用层总发送预算8MB/s，最多积攒4包；音频优先，不代表实测吞吐。 */
#define M16_IO_RATE_BYTES 8000000u
#define M16_IO_BURST_BYTES 4336u
#define M16_IO_INTERVAL_US 100u
#endif
