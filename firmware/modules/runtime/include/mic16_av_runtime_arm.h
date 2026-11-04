#ifndef MIC16_AV_RUNTIME_ARM_H
#define MIC16_AV_RUNTIME_ARM_H
#include "../../mic16_cooperate.h"
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
int m16_runtime_arm_start(void);
/* 供整链复合检查点复用现有视频/网络服务，不重新绑定或初始化。 */
void m16_runtime_arm_service(void);
int m16_runtime_arm_network(void);
int m16_runtime_arm_pause_capture(void);
void m16_runtime_arm_resume_capture(void);
int m16_runtime_arm_audio_send(void *,const unsigned char *,unsigned);
void m16_runtime_arm_diagnostics(void);
#endif
#endif
