#ifndef MIC16_CACHE_SLICES_H
#define MIC16_CACHE_SLICES_H
#include <stdint.h>
/* 每次最多1024字节；实际IRQ屏蔽时长仍须实板量测。
 * checkpoint非零时停止，不能提交尚未完整刷新的结果。 */
int mic16_cache_flush_slices(uintptr_t,unsigned,void (*)(void *,uintptr_t,unsigned),int (*)(void *),void *);
#endif
