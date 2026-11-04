#ifndef MIC16_SHARED_SPECTRUM_DISPATCH_H
#define MIC16_SHARED_SPECTRUM_DISPATCH_H
#include "mic16_shared_spectrum_runtime.h"
typedef struct {
 unsigned calls,completed,cpu_disabled,cpu_range,batches,errors;
 uint64_t cache_ticks,device_ticks,invalidate_ticks,total_ticks;
} m16_shared_dispatch_report;
extern m16_shared_dispatch_report m16_shared_dispatch_counts;
/* 默认关闭；只有匹配硬件已初始化且空闲，bind/select才允许启用。
 * 单主线程所有权；超时隔离不能靠重绑或关开模式清除。
 * clear仅清计数，不清硬件状态。硬件错误返回给上层，不伪装CPU加速成绩。 */
int m16_shared_dispatch_bind(m16_shared_runtime *);
int m16_shared_dispatch_select(unsigned);
void m16_shared_dispatch_clear(void);
#endif
