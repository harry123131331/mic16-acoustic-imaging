#ifndef MIC16_FEATURE_LAYOUT_CHECK_H
#define MIC16_FEATURE_LAYOUT_CHECK_H
#include <stddef.h>
#include "mic16_physical.h"
/* 保留改动前的字段顺序，在真实ARM编译选项下检查大小、后续字段偏移与缓存容量。 */
typedef struct {
    mic16_spectral_workspace spectral;mic16_spectral_frame frame;mic16_spectral_reference reference;
    float phat[20*807*32*2];double levels[20*24],powers[807],scratch[2048];
    float beam[40960],quiet_beam[8192],signature[32*34*2];
    double bands[20*32],beam_level[20],quiet_level[4],errors[480];
} mic16_prior_physical_layout;
typedef char mic16_same_physical_size[(sizeof(mic16_prior_physical_layout)==sizeof(mic16_physical_workspace))?1:-1];
typedef char mic16_same_phat_offset[(offsetof(mic16_prior_physical_layout,phat)==offsetof(mic16_physical_workspace,phat))?1:-1];
typedef char mic16_same_levels_offset[(offsetof(mic16_prior_physical_layout,levels)==offsetof(mic16_physical_workspace,levels))?1:-1];
typedef char mic16_reuse_storage_fits[(sizeof(mic16_statistics_reuse_storage)<=sizeof(((mic16_physical_workspace *)0)->phat))?1:-1];
#endif
