#include "mic16_tf_zone_streaming_hls_q2_24.h"

typedef struct {
    mic16_tf_q2_24_t re;
    mic16_tf_q2_24_t im;
} mic16_complex_q2_24_t;

static const unsigned short k_zone_offsets_q2_24[MIC16_TF_HLS_ZONE_COUNT + 1U] = {
    0U, 34U, 68U, 102U, 136U, 170U, 204U, 238U, 272U,
    306U, 340U, 374U, 408U, 442U, 476U, 510U, 544U,
    578U, 612U, 646U, 680U, 714U, 748U, 782U, 807U
};

static mic16_tf_q2_24_t unpack_component(
    mic16_tf_packed_complex_q2_24_t packed,
    unsigned int high,
    unsigned int low)
{
#pragma HLS INLINE
    mic16_tf_q2_24_t value;
    value.range(25, 0) = packed.range(high, low);
    return value;
}

static mic16_tf_packed_score_q2_24_t pack_score(mic16_tf_q2_24_t value)
{
#pragma HLS INLINE
    ap_int<26> raw;
    raw.range(25, 0) = value.range(25, 0);
    return (mic16_tf_packed_score_q2_24_t)raw;
}

void mic16_tf_zone_streaming_hls_q2_24(
    ap_uint<64> *ddr_arena,
    unsigned int observation_offset,
    unsigned int initial_phasor_offset,
    unsigned int bin_rotator_offset,
    unsigned int output_offset)
{
#pragma HLS INTERFACE m_axi port=ddr_arena offset=slave bundle=gmem0 depth=50524
#pragma HLS INTERFACE s_axilite port=ddr_arena bundle=control
#pragma HLS INTERFACE s_axilite port=observation_offset bundle=control
#pragma HLS INTERFACE s_axilite port=initial_phasor_offset bundle=control
#pragma HLS INTERFACE s_axilite port=bin_rotator_offset bundle=control
#pragma HLS INTERFACE s_axilite port=output_offset bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    static mic16_complex_q2_24_t
        phasor[MIC16_TF_Q2_24_PADDED_GRID_COUNT][MIC16_TF_HLS_PAIR_COUNT];
    static mic16_complex_q2_24_t
        rotator[MIC16_TF_Q2_24_PADDED_GRID_COUNT][MIC16_TF_HLS_PAIR_COUNT];
    mic16_tf_accum_q2_24_t accumulator[MIC16_TF_Q2_24_PADDED_GRID_COUNT];
    mic16_complex_q2_24_t observation_cache[MIC16_TF_HLS_PAIR_COUNT];
    ap_uint<64> packed_score_word = 0;

#pragma HLS ARRAY_PARTITION variable=phasor cyclic factor=2 dim=1
#pragma HLS ARRAY_PARTITION variable=phasor cyclic factor=2 dim=2
#pragma HLS ARRAY_PARTITION variable=rotator cyclic factor=2 dim=1
#pragma HLS ARRAY_PARTITION variable=rotator cyclic factor=2 dim=2
#pragma HLS ARRAY_PARTITION variable=accumulator cyclic factor=2 dim=1
#pragma HLS ARRAY_PARTITION variable=observation_cache cyclic factor=2 dim=1

load_state_grid_q2_24:
    for (unsigned int grid_index = 0U;
         grid_index < MIC16_TF_Q2_24_PADDED_GRID_COUNT;
         ++grid_index) {
    load_state_pair_q2_24:
        for (unsigned int pair_index = 0U; pair_index < MIC16_TF_HLS_PAIR_COUNT; ++pair_index) {
#pragma HLS PIPELINE II=1
            if (grid_index < MIC16_TF_HLS_GRID_COUNT) {
                const unsigned int state_index = grid_index * MIC16_TF_HLS_PAIR_COUNT + pair_index;
                const mic16_tf_packed_complex_q2_24_t phase_word =
                    ddr_arena[initial_phasor_offset + state_index];
                const mic16_tf_packed_complex_q2_24_t step_word =
                    ddr_arena[bin_rotator_offset + state_index];
                phasor[grid_index][pair_index].re = unpack_component(phase_word, 25U, 0U);
                phasor[grid_index][pair_index].im = unpack_component(phase_word, 51U, 26U);
                rotator[grid_index][pair_index].re = unpack_component(step_word, 25U, 0U);
                rotator[grid_index][pair_index].im = unpack_component(step_word, 51U, 26U);
            } else {
                phasor[grid_index][pair_index].re = 0;
                phasor[grid_index][pair_index].im = 0;
                rotator[grid_index][pair_index].re = 1;
                rotator[grid_index][pair_index].im = 0;
            }
        }
    }

zone_loop_q2_24:
    for (unsigned int zone_index = 0U; zone_index < MIC16_TF_HLS_ZONE_COUNT; ++zone_index) {
    clear_accumulator_q2_24:
        for (unsigned int grid_index = 0U;
             grid_index < MIC16_TF_Q2_24_PADDED_GRID_COUNT;
             ++grid_index) {
#pragma HLS PIPELINE II=1
            accumulator[grid_index] = 0;
        }

    bin_loop_q2_24:
        for (unsigned int bin_index = (unsigned int)k_zone_offsets_q2_24[zone_index];
             bin_index < (unsigned int)k_zone_offsets_q2_24[zone_index + 1U];
             ++bin_index) {
        load_observation_q2_24:
            for (unsigned int pair_index = 0U; pair_index < MIC16_TF_HLS_PAIR_COUNT; ++pair_index) {
#pragma HLS PIPELINE II=1
                const mic16_tf_packed_complex_q2_24_t word =
                    ddr_arena[observation_offset
                        + bin_index * MIC16_TF_HLS_PAIR_COUNT + pair_index];
                observation_cache[pair_index].re = unpack_component(word, 25U, 0U);
                observation_cache[pair_index].im = unpack_component(word, 51U, 26U);
            }

        grid_batch_loop_q2_24:
            for (unsigned int grid_base = 0U;
                 grid_base < MIC16_TF_Q2_24_PADDED_GRID_COUNT;
                 grid_base += MIC16_TF_Q2_24_GRID_LANES) {
                mic16_tf_accum_q2_24_t
                    partial[MIC16_TF_Q2_24_GRID_LANES][MIC16_TF_HLS_ACCUM_SLOTS];
#pragma HLS ARRAY_PARTITION variable=partial complete dim=0

            reset_partial_q2_24:
                for (unsigned int slot = 0U; slot < MIC16_TF_HLS_ACCUM_SLOTS; ++slot) {
#pragma HLS UNROLL
                    partial[0][slot] = 0;
                    partial[1][slot] = 0;
                }

            pair_group_loop_q2_24:
                for (unsigned int group = 0U; group < MIC16_TF_Q2_24_PAIR_GROUPS; ++group) {
#pragma HLS PIPELINE II=1
                    mic16_tf_accum_q2_24_t lane_value[MIC16_TF_Q2_24_GRID_LANES];
#pragma HLS ARRAY_PARTITION variable=lane_value complete dim=1

                lane_compute_q2_24:
                    for (unsigned int lane = 0U; lane < MIC16_TF_Q2_24_GRID_LANES; ++lane) {
#pragma HLS UNROLL
                        const unsigned int grid_index = grid_base + lane;
                        const unsigned int pair0 = group * MIC16_TF_Q2_24_PAIR_LANES;
                        const unsigned int pair1 = pair0 + 1U;
                        const mic16_complex_q2_24_t phase0 = phasor[grid_index][pair0];
                        const mic16_complex_q2_24_t phase1 = phasor[grid_index][pair1];
                        const mic16_complex_q2_24_t observed0 = observation_cache[pair0];
                        const mic16_complex_q2_24_t observed1 = observation_cache[pair1];
                        const mic16_complex_q2_24_t step0 = rotator[grid_index][pair0];
                        const mic16_complex_q2_24_t step1 = rotator[grid_index][pair1];
                        mic16_complex_q2_24_t advanced0;
                        mic16_complex_q2_24_t advanced1;
                        const mic16_tf_accum_q2_24_t dot0 =
                            phase0.re * observed0.re + phase0.im * observed0.im;
                        const mic16_tf_accum_q2_24_t dot1 =
                            phase1.re * observed1.re + phase1.im * observed1.im;

                        lane_value[lane] = dot0 + dot1;
                        advanced0.re = phase0.re * step0.re - phase0.im * step0.im;
                        advanced0.im = phase0.re * step0.im + phase0.im * step0.re;
                        advanced1.re = phase1.re * step1.re - phase1.im * step1.im;
                        advanced1.im = phase1.re * step1.im + phase1.im * step1.re;
                        phasor[grid_index][pair0] = advanced0;
                        phasor[grid_index][pair1] = advanced1;
                    }

#define MIC16_Q2_24_ACCUMULATE_SLOT(SLOT) \
                    partial[0][SLOT] += lane_value[0]; \
                    partial[1][SLOT] += lane_value[1]
                    switch (group & 7U) {
                    case 0U: MIC16_Q2_24_ACCUMULATE_SLOT(0); break;
                    case 1U: MIC16_Q2_24_ACCUMULATE_SLOT(1); break;
                    case 2U: MIC16_Q2_24_ACCUMULATE_SLOT(2); break;
                    case 3U: MIC16_Q2_24_ACCUMULATE_SLOT(3); break;
                    case 4U: MIC16_Q2_24_ACCUMULATE_SLOT(4); break;
                    case 5U: MIC16_Q2_24_ACCUMULATE_SLOT(5); break;
                    case 6U: MIC16_Q2_24_ACCUMULATE_SLOT(6); break;
                    default: MIC16_Q2_24_ACCUMULATE_SLOT(7); break;
                    }
#undef MIC16_Q2_24_ACCUMULATE_SLOT
                }

                const mic16_tf_accum_q2_24_t lane0_sum =
                    ((partial[0][0] + partial[0][1]) + (partial[0][2] + partial[0][3])) +
                    ((partial[0][4] + partial[0][5]) + (partial[0][6] + partial[0][7]));
                const mic16_tf_accum_q2_24_t lane1_sum =
                    ((partial[1][0] + partial[1][1]) + (partial[1][2] + partial[1][3])) +
                    ((partial[1][4] + partial[1][5]) + (partial[1][6] + partial[1][7]));
                accumulator[grid_base] += lane0_sum;
                accumulator[grid_base + 1U] += lane1_sum;
            }
        }

        const mic16_tf_norm_q2_24_t normalization =
            (zone_index + 1U == MIC16_TF_HLS_ZONE_COUNT)
                ? mic16_tf_norm_q2_24_t(1.0 / 800.0)
                : mic16_tf_norm_q2_24_t(1.0 / 1088.0);
    write_zone_scores_q2_24:
        for (unsigned int grid_index = 0U; grid_index < MIC16_TF_HLS_GRID_COUNT; ++grid_index) {
#pragma HLS PIPELINE II=1
            const mic16_tf_q2_24_t score = accumulator[grid_index] * normalization;
            const unsigned int score_index =
                zone_index * MIC16_TF_HLS_GRID_COUNT + grid_index;
            const mic16_tf_packed_score_q2_24_t raw_score = pack_score(score);
            if ((score_index & 1U) == 0U) {
                packed_score_word = 0;
                packed_score_word.range(31, 0) = raw_score.range(31, 0);
            } else {
                packed_score_word.range(63, 32) = raw_score.range(31, 0);
                ddr_arena[output_offset + (score_index >> 1U)] =
                    packed_score_word;
            }
        }
    }
}
