#ifndef MIC16_STATISTICS_TILE_H
#define MIC16_STATISTICS_TILE_H
#include <stdint.h>
/* 原型ABI：64位字实部在低32位、虚部在高32位；所有偏移以64位字计。
 * 观测[4帧][8频点][32通道对]，系数[8频点][32通道对][4候选]。
 * 输出[4帧][8频点][4候选][短/长基线]；每字为一个复数和。
 * 固定槽尺寸分别为1024、1024、256字。无效尾槽保持调用前内容。
 * 返回0成功、-1参数/重叠错误、-2非有限或分量超界，错误不写输出。
 * 仅实现原统计FP32旋转，未接生产固件、扫描归约或板卡。 */
#ifdef __cplusplus
extern "C" {
#endif
int m16_statistics_tile(uint64_t *arena, unsigned arena_words,
    unsigned observations, unsigned coefficients, unsigned output,
    unsigned frames, unsigned bin_count, unsigned candidates);
#ifdef __cplusplus
}
#endif
#endif
