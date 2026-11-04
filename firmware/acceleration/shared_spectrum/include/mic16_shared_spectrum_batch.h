#ifndef MIC16_SHARED_SPECTRUM_BATCH_H
#define MIC16_SHARED_SPECTRUM_BATCH_H
#include "mic16_shared_spectrum_pack.h"
#include "mic16_physical.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 回调必须同步完成模式4，或在失败后隔离硬件；返回后调用者不能把半成品当作结果。
 * 非零回调返回使本次检测失败，禁止直接复用尚未确认停止的DMA缓冲。
 * arena/输入/输出/work必须分离。上层先确认后端ready，再调用本函数准备arena。 */
typedef int (*m16_shared_spectrum_execute)(void *,uint64_t *,unsigned);
/* 与原单通道规则一致：4或20帧，每帧2048点，原FP64均值/能量/Hann/32频带。
 * 0成功，1需由上层整候选重新跑原CPU路径，-1输入，-2后端/输出错误。
 * 错误或回退时，已有levels/bands可能为部分结果，必须全部丢弃；回退不跳过声源。
 * 本函数不访问MMIO，不负责cache或IRQ，不改变512点统计和公共定位FFT。 */
int m16_shared_mono_bands(const float *,unsigned,double *,double *,mic16_physical_workspace *,
 uint64_t *,size_t,m16_shared_spectrum_execute,void *);
#ifdef __cplusplus
}
#endif
#endif
