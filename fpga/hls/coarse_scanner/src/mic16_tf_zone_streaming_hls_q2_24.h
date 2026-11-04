#ifndef MIC16_TF_ZONE_STREAMING_HLS_Q2_24_H
#define MIC16_TF_ZONE_STREAMING_HLS_Q2_24_H

#include "ap_fixed.h"
#include "ap_int.h"
#include "mic16_tf_zone_streaming_hls.h"

#define MIC16_TF_Q2_24_PADDED_GRID_COUNT 326U
#define MIC16_TF_Q2_24_GRID_LANES 2U
#define MIC16_TF_Q2_24_PAIR_LANES 2U
#define MIC16_TF_Q2_24_PAIR_GROUPS 16U
#define MIC16_TF_Q2_24_PACKED_SCORE_WORD_COUNT \
    ((MIC16_TF_HLS_SCORE_COUNT + 1U) / 2U)
#define MIC16_TF_Q2_24_OBSERVATION_OFFSET 0U
#define MIC16_TF_Q2_24_INITIAL_PHASOR_OFFSET \
    (MIC16_TF_Q2_24_OBSERVATION_OFFSET + MIC16_TF_HLS_OBSERVATION_COUNT)
#define MIC16_TF_Q2_24_BIN_ROTATOR_OFFSET \
    (MIC16_TF_Q2_24_INITIAL_PHASOR_OFFSET + MIC16_TF_HLS_STATE_COUNT)
#define MIC16_TF_Q2_24_OUTPUT_OFFSET \
    (MIC16_TF_Q2_24_BIN_ROTATOR_OFFSET + MIC16_TF_HLS_STATE_COUNT)
#define MIC16_TF_Q2_24_ARENA_WORD_COUNT \
    (MIC16_TF_Q2_24_OUTPUT_OFFSET + MIC16_TF_Q2_24_PACKED_SCORE_WORD_COUNT)

typedef ap_fixed<26, 2, AP_RND_CONV, AP_SAT> mic16_tf_q2_24_t;
typedef ap_fixed<48, 16, AP_RND_CONV, AP_SAT> mic16_tf_accum_q2_24_t;
typedef ap_fixed<40, 2, AP_RND_CONV, AP_SAT> mic16_tf_norm_q2_24_t;
typedef ap_uint<64> mic16_tf_packed_complex_q2_24_t;
typedef ap_int<32> mic16_tf_packed_score_q2_24_t;

void mic16_tf_zone_streaming_hls_q2_24(
    ap_uint<64> *ddr_arena,
    unsigned int observation_offset,
    unsigned int initial_phasor_offset,
    unsigned int bin_rotator_offset,
    unsigned int output_offset);

#endif
