#include <math.h>
#include <stdio.h>

#include "mic16_tf_zone_streaming_hls_q2_24.h"

static mic16_complex_f32_t observation_f32[MIC16_TF_HLS_OBSERVATION_COUNT];
static mic16_complex_f32_t initial_phasor_f32[MIC16_TF_HLS_STATE_COUNT];
static mic16_complex_f32_t bin_rotator_f32[MIC16_TF_HLS_STATE_COUNT];
static ap_uint<64> ddr_arena[MIC16_TF_Q2_24_ARENA_WORD_COUNT];
static float expected_scores[MIC16_TF_HLS_SCORE_COUNT];
static unsigned short expected_peaks[MIC16_TF_HLS_ZONE_COUNT];

static int load_file(const char *root, const char *name, void *data, size_t bytes)
{
    char path[1024];
    FILE *stream;
    size_t count;
    if (snprintf(path, sizeof(path), "%s/%s", root, name) <= 0) return 0;
    stream = fopen(path, "rb");
    if (stream == NULL) return 0;
    count = fread(data, 1U, bytes, stream);
    fclose(stream);
    return count == bytes;
}

static mic16_tf_packed_complex_q2_24_t pack_complex(mic16_complex_f32_t source)
{
    const mic16_tf_q2_24_t real_value = source.re;
    const mic16_tf_q2_24_t imag_value = source.im;
    mic16_tf_packed_complex_q2_24_t packed = 0;
    packed.range(25, 0) = real_value.range(25, 0);
    packed.range(51, 26) = imag_value.range(25, 0);
    return packed;
}

static float unpack_score(mic16_tf_packed_score_q2_24_t packed)
{
    mic16_tf_q2_24_t value;
    value.range(25, 0) = packed.range(25, 0);
    return (float)value;
}

static mic16_tf_packed_score_q2_24_t load_actual_score(unsigned int score_index)
{
    const ap_uint<64> word =
        ddr_arena[MIC16_TF_Q2_24_OUTPUT_OFFSET + (score_index >> 1U)];
    mic16_tf_packed_score_q2_24_t score;
    if ((score_index & 1U) == 0U)
        score.range(31, 0) = word.range(31, 0);
    else
        score.range(31, 0) = word.range(63, 32);
    return score;
}

int main(int argc, char **argv)
{
    unsigned int index;
    unsigned int zone_index;
    unsigned int grid_index;
    unsigned int score_mismatches = 0U;
    unsigned int peak_mismatches = 0U;
    float maximum_abs_error = 0.0f;
    const float tolerance = 5.0e-5f;
    const char *root = (argc > 1) ? argv[1] : ".";

    if (!load_file(root, "d3c_observation.bin", observation_f32, sizeof(observation_f32)) ||
        !load_file(root, "d3c_initial_phasor.bin", initial_phasor_f32, sizeof(initial_phasor_f32)) ||
        !load_file(root, "d3c_bin_rotator.bin", bin_rotator_f32, sizeof(bin_rotator_f32)) ||
        !load_file(root, "d3c_expected_scores.bin", expected_scores, sizeof(expected_scores)) ||
        !load_file(root, "d3c_expected_peaks.bin", expected_peaks, sizeof(expected_peaks))) {
        printf("D3C_Q2_24_FAIL_GOLDEN_LOAD\n");
        return 1;
    }
    for (index = 0U; index < MIC16_TF_HLS_OBSERVATION_COUNT; ++index)
        ddr_arena[MIC16_TF_Q2_24_OBSERVATION_OFFSET + index] =
            pack_complex(observation_f32[index]);
    for (index = 0U; index < MIC16_TF_HLS_STATE_COUNT; ++index) {
        ddr_arena[MIC16_TF_Q2_24_INITIAL_PHASOR_OFFSET + index] =
            pack_complex(initial_phasor_f32[index]);
        ddr_arena[MIC16_TF_Q2_24_BIN_ROTATOR_OFFSET + index] =
            pack_complex(bin_rotator_f32[index]);
    }

    mic16_tf_zone_streaming_hls_q2_24(
        ddr_arena,
        MIC16_TF_Q2_24_OBSERVATION_OFFSET,
        MIC16_TF_Q2_24_INITIAL_PHASOR_OFFSET,
        MIC16_TF_Q2_24_BIN_ROTATOR_OFFSET,
        MIC16_TF_Q2_24_OUTPUT_OFFSET);
    for (zone_index = 0U; zone_index < MIC16_TF_HLS_ZONE_COUNT; ++zone_index) {
        unsigned int peak = 0U;
        float peak_score = unpack_score(
            load_actual_score(zone_index * MIC16_TF_HLS_GRID_COUNT));
        for (grid_index = 0U; grid_index < MIC16_TF_HLS_GRID_COUNT; ++grid_index) {
            const unsigned int score_index =
                zone_index * MIC16_TF_HLS_GRID_COUNT + grid_index;
            const float actual = unpack_score(load_actual_score(score_index));
            const float error = fabsf(actual - expected_scores[score_index]);
            if (error > maximum_abs_error) maximum_abs_error = error;
            if (error > tolerance) ++score_mismatches;
            if (actual > peak_score) {
                peak = grid_index;
                peak_score = actual;
            }
        }
        if (peak != (unsigned int)expected_peaks[zone_index]) ++peak_mismatches;
    }
    printf(
        "D3C_Q2_24_RESULT score_count=%u zone_count=%u max_abs_error=%.9g "
        "score_mismatches=%u peak_mismatches=%u\n",
        MIC16_TF_HLS_SCORE_COUNT,
        MIC16_TF_HLS_ZONE_COUNT,
        (double)maximum_abs_error,
        score_mismatches,
        peak_mismatches);
    if ((score_mismatches != 0U) || (peak_mismatches != 0U)) return 1;
    printf("D3C_Q2_24_PASS_FULL_GOLDEN\n");
    return 0;
}
