#ifndef MIC16_SHARED_SPECTRUM_PACK_H
#define MIC16_SHARED_SPECTRUM_PACK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 连续变换布局：每列2048个F32连续存放，两样本/64位字。
 * 只供后续明确支持该布局的模式使用，不能交给当前交错布局模式1。
 * 0成功，1要求保留原FP64路径，-1参数错误。失败不改arena。
 * cache维护、核执行、完成和超时隔离由上层驱动负责，本模块不访问MMIO。 */
#define M16_SHARED_ARENA_WORDS 80944u
#define M16_SHARED_WORKSPACE_WORD 16384u
#define M16_SHARED_SAMPLES 2048u
#define M16_SHARED_BINS 807u
int m16_shared_pack_contiguous(const double *window,unsigned slot,uint64_t *arena,size_t words);
/* 实低32/虚高32，解包到807个FP64复数；不做频谱尺度或门限修改。
 * 输出数组必须独立且不与arena重叠。失败不改输出。 */
int m16_shared_read_bins(const uint64_t *arena,size_t words,unsigned slot,double *real,double *imag);
#ifdef __cplusplus
}
#endif
#endif
