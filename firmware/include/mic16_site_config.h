#ifndef MIC16_SITE_CONFIG_H
#define MIC16_SITE_CONFIG_H
/* 完成实际数字幅值及通道对应检查后才能修改，并重新构建。0表示未配置；-1用于禁止擅自猜测除以2^31或2^18。数字归一化不代表声压级SPL标定。 */
#define MIC16_SITE_INPUT_VERIFIED 0
#define MIC16_SITE_SCALE_EXPONENT (-1)
#define MIC16_SITE_LOGICAL_TO_WIRE {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}
#define MIC16_SITE_POSE_REFERENCE_ID 1ULL
#define MIC16_BURST_WINDOWS 3u
#endif
