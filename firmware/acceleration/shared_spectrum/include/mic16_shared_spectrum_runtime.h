#ifndef MIC16_SHARED_SPECTRUM_RUNTIME_H
#define MIC16_SHARED_SPECTRUM_RUNTIME_H
#include "mic16_shared_spectrum_batch.h"
#include "mic16_scanner_hls.h"
enum {M16_SHARED_INVALID=-1,M16_SHARED_BUSY=-2,M16_SHARED_TIMEOUT=-3,
 M16_SHARED_GUARD=-4,M16_SHARED_CLOCK=-5,M16_SHARED_ABI=-6,M16_SHARED_RETURN=-7};
typedef struct {
 uint64_t before[8],words[M16_SHARED_ARENA_WORDS],after[8];
 m16_scanner_io io;unsigned initialized,quarantined,busy;
} m16_shared_runtime;
typedef struct {
 uint64_t cache_ticks,device_ticks,invalidate_ticks,total_ticks;
 unsigned completed_batches,active_channels,last_control,quarantined;
 int kernel_return;
} m16_shared_report;
/* 首次使用必须清零且64字节对齐；仅供单所有者主线程，不得从ISR或并发任务调用。
 * init只在idle时探测mode/active输入寄存器并恢复，不写ap_start或DDR指针。
 * 已核验旧前端地址宽6，0x3c保留、0x44折回GIE；探测后恢复GIE，不启动旧DMA。
 * 这不是任意未知IP的安全探测；只能用于本工程已知的新/旧前端与匹配下载清单。
 * 超时后隔离，硬件确认停止前不准清除quarantined或复用arena。 */
int m16_shared_runtime_init(m16_shared_runtime *,const m16_scanner_io *);
int m16_shared_runtime_available(const m16_shared_runtime *);
int m16_shared_runtime_execute(m16_shared_runtime *,unsigned,m16_shared_report *);
/* ARM平台包装只在对应可选构建链接，不自动启用模式4。 */
int m16_shared_runtime_platform_init(m16_shared_runtime *);
#endif
