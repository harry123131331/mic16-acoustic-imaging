#ifndef MIC16_TF_PHAT_FRONTEND_HLS_Q2_24_H
#define MIC16_TF_PHAT_FRONTEND_HLS_Q2_24_H

#include "ap_fixed.h"
#include "ap_int.h"
#include "mic16_ssl_hls_config.h"

#define MIC16_TF_FRONTEND_FIRST_BIN 32U
#define MIC16_TF_FRONTEND_LAST_BIN 838U
#define MIC16_TF_FRONTEND_BIN_COUNT 807U
#define MIC16_TF_FRONTEND_SPECTRUM_COUNT \
    (MIC16_HLS_CHANNELS * MIC16_TF_FRONTEND_BIN_COUNT)
#define MIC16_TF_FRONTEND_OBSERVATION_COUNT \
    (MIC16_TF_FRONTEND_BIN_COUNT * MIC16_HLS_PAIR_COUNT)
#define MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS 32U
#define MIC16_TF_FRONTEND_SPECTRUM_OFFSET 0U
#define MIC16_TF_FRONTEND_PAIR_SCRATCH_OFFSET \
    (MIC16_TF_FRONTEND_SPECTRUM_OFFSET + MIC16_TF_FRONTEND_SPECTRUM_COUNT)
#define MIC16_TF_FRONTEND_OUTPUT_OFFSET \
    (MIC16_TF_FRONTEND_PAIR_SCRATCH_OFFSET + MIC16_TF_FRONTEND_OBSERVATION_COUNT)
#define MIC16_TF_FRONTEND_WORKSPACE_COUNT \
    (MIC16_TF_FRONTEND_OUTPUT_OFFSET + MIC16_TF_FRONTEND_OBSERVATION_COUNT)
#define MIC16_TF_FRONTEND_PCM_WORD_COUNT \
    ((MIC16_HLS_FRAME_SAMPLES * MIC16_HLS_CHANNELS) / 2U)
#define MIC16_TF_FRONTEND_ARENA_WORD_COUNT \
    (MIC16_TF_FRONTEND_PCM_WORD_COUNT + MIC16_TF_FRONTEND_WORKSPACE_COUNT)

typedef ap_fixed<26, 2, AP_RND_CONV, AP_SAT> mic16_tf_frontend_q2_24_t;
typedef ap_uint<64> mic16_tf_frontend_packed_complex_q2_24_t;
typedef ap_uint<64> mic16_tf_frontend_packed_complex_f32_t;

// Short exported symbol keeps Vivado 2020.2 generated FFT paths below the
// Windows MAX_PATH limit.  The numerical/interface contract is unchanged.
void m16_tf_front_q24(
    ap_uint<64> *ddr_arena,
    unsigned int pcm_word_offset,
    unsigned int workspace_word_offset,
    unsigned int frame_samples);

#endif
