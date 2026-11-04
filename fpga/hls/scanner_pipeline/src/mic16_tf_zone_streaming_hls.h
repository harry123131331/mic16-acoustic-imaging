#ifndef MIC16_TF_ZONE_STREAMING_HLS_H
#define MIC16_TF_ZONE_STREAMING_HLS_H

#include "mic16_tf_zone_streaming_core.h"

#define MIC16_TF_HLS_GRID_COUNT 325U
#define MIC16_TF_HLS_PAIR_COUNT 32U
#define MIC16_TF_HLS_BIN_COUNT 807U
#define MIC16_TF_HLS_ZONE_COUNT 24U
#define MIC16_TF_HLS_GRID_LANES 2U
#define MIC16_TF_HLS_ACCUM_SLOTS 8U

#define MIC16_TF_HLS_OBSERVATION_COUNT \
    (MIC16_TF_HLS_BIN_COUNT * MIC16_TF_HLS_PAIR_COUNT)
#define MIC16_TF_HLS_STATE_COUNT \
    (MIC16_TF_HLS_GRID_COUNT * MIC16_TF_HLS_PAIR_COUNT)
#define MIC16_TF_HLS_SCORE_COUNT \
    (MIC16_TF_HLS_ZONE_COUNT * MIC16_TF_HLS_GRID_COUNT)

#ifdef __cplusplus
extern "C" {
#endif

void mic16_tf_zone_streaming_hls(
    const mic16_complex_f32_t observation_bin_pair[MIC16_TF_HLS_OBSERVATION_COUNT],
    const mic16_complex_f32_t initial_phasor[MIC16_TF_HLS_STATE_COUNT],
    const mic16_complex_f32_t bin_rotator[MIC16_TF_HLS_STATE_COUNT],
    float output_zone_grid[MIC16_TF_HLS_SCORE_COUNT]);

#ifdef __cplusplus
}
#endif

#endif
