#ifndef MIC16_SIGNATURE_RECURRENCE_H
#define MIC16_SIGNATURE_RECURRENCE_H
#include "mic16_spatial.h"
/* 仅在窗口边界切换或使转子缓存失效；不改变空间网格、频率或累加顺序。 */
/* 计数器依次为生成的系数对、原库回退对、频带块、转子、零相位对；不逐命中写计数。 */
extern unsigned m16_signature_recurrence_enabled,m16_signature_recurrence_calls[5];
int m16_signature_recurrence_select(unsigned);
void m16_signature_recurrence_clear(void);
int m16_signature_recurrence(unsigned,unsigned,unsigned,float *);
#endif
