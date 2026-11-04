#ifndef MIC16_RESPONSE_NEON_H
#define MIC16_RESPONSE_NEON_H
#include "mic16_physical.h"
extern unsigned m16_response_neon_mode,m16_response_neon_calls[3];
int m16_response_neon_select(unsigned mode);
/* 物理特征每次调用重置所有者；统计阶段会覆盖PHAT，禁止跨窗复用。 */
void m16_response_neon_begin(void);
void m16_response_neon(const float *,float *,mic16_physical_workspace *);
#endif
