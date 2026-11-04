#include "mic16_tf_phat_frontend_hls_q2_24.h"

#include <math.h>

#include "mic16_fft_engine_hls.h"
#include "mic16_hann_2048_f32.h"

union mic16_tf_frontend_float_bits_t {
    float f;
    unsigned int u;
};

static mic16_tf_frontend_packed_complex_f32_t pack_complex_f32(
    mic16_complex_t value)
{
#pragma HLS INLINE
    mic16_tf_frontend_float_bits_t real_bits;
    mic16_tf_frontend_float_bits_t imag_bits;
    real_bits.f = value.re;
    imag_bits.f = value.im;
    mic16_tf_frontend_packed_complex_f32_t packed = 0;
    packed.range(31, 0) = real_bits.u;
    packed.range(63, 32) = imag_bits.u;
    return packed;
}

static mic16_complex_t unpack_complex_f32(
    mic16_tf_frontend_packed_complex_f32_t packed)
{
#pragma HLS INLINE
    mic16_tf_frontend_float_bits_t real_bits;
    mic16_tf_frontend_float_bits_t imag_bits;
    real_bits.u = (unsigned int)packed.range(31, 0);
    imag_bits.u = (unsigned int)packed.range(63, 32);
    mic16_complex_t value = {real_bits.f, imag_bits.f};
    return value;
}

static mic16_tf_frontend_packed_complex_q2_24_t pack_q2_24(
    float real_value,
    float imag_value)
{
#pragma HLS INLINE
    const mic16_tf_frontend_q2_24_t real_fixed = real_value;
    const mic16_tf_frontend_q2_24_t imag_fixed = imag_value;
    mic16_tf_frontend_packed_complex_q2_24_t packed = 0;
    packed.range(25, 0) = real_fixed.range(25, 0);
    packed.range(51, 26) = imag_fixed.range(25, 0);
    return packed;
}

static mic16_sample_t load_packed_pcm(
    const ap_uint<64> *ddr_arena,
    unsigned int pcm_word_offset,
    unsigned int sample_index)
{
#pragma HLS INLINE
    const ap_uint<64> word =
        ddr_arena[pcm_word_offset + (sample_index >> 1U)];
    ap_int<32> value;
    if ((sample_index & 1U) == 0U)
        value.range(31, 0) = word.range(31, 0);
    else
        value.range(31, 0) = word.range(63, 32);
    return (mic16_sample_t)value;
}

void m16_tf_front_q24(
    ap_uint<64> *ddr_arena,
    unsigned int pcm_word_offset,
    unsigned int workspace_word_offset,
    unsigned int frame_samples)
{
#pragma HLS INTERFACE m_axi port=ddr_arena offset=slave bundle=gmem0 depth=80944
#pragma HLS INTERFACE s_axilite port=ddr_arena bundle=control
#pragma HLS INTERFACE s_axilite port=pcm_word_offset bundle=control
#pragma HLS INTERFACE s_axilite port=workspace_word_offset bundle=control
#pragma HLS INTERFACE s_axilite port=frame_samples bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    static mic16_complex_t fft_buffer[MIC16_HLS_FFT_LEN];
    static mic16_complex_t pair_spectrum_a[MIC16_TF_FRONTEND_BIN_COUNT];
    static mic16_complex_t pair_spectrum_b[MIC16_TF_FRONTEND_BIN_COUNT];
    static mic16_tf_frontend_packed_complex_q2_24_t transpose_tile[
        MIC16_HLS_PAIR_COUNT * MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS];
    const unsigned int sample_count =
        (frame_samples == 0U || frame_samples > MIC16_HLS_FRAME_SAMPLES)
            ? MIC16_HLS_FRAME_SAMPLES
            : frame_samples;

channel_fft_loop:
    for (unsigned int channel = 0U; channel < MIC16_HLS_CHANNELS; ++channel) {
        long long mean_sum = 0LL;

    channel_mean_loop:
        for (unsigned int sample = 0U; sample < MIC16_HLS_FRAME_SAMPLES; ++sample) {
#pragma HLS PIPELINE II=1
            if (sample < sample_count)
                mean_sum += (long long)load_packed_pcm(
                    ddr_arena,
                    pcm_word_offset,
                    sample * MIC16_HLS_CHANNELS + channel);
        }
        const float mean = (float)mean_sum / (float)sample_count;

    channel_window_loop:
        for (unsigned int sample = 0U; sample < MIC16_HLS_FRAME_SAMPLES; ++sample) {
#pragma HLS PIPELINE II=1
            float value = 0.0f;
            if (sample < sample_count) {
                value = ((float)load_packed_pcm(
                    ddr_arena,
                    pcm_word_offset,
                    sample * MIC16_HLS_CHANNELS + channel) - mean)
                    * MIC16_HANN_2048_F32[sample];
            }
            fft_buffer[sample].re = value;
            fft_buffer[sample].im = 0.0f;
        }

    channel_zero_pad_loop:
        for (unsigned int sample = MIC16_HLS_FRAME_SAMPLES;
             sample < MIC16_HLS_FFT_LEN;
             ++sample) {
#pragma HLS PIPELINE II=1
            fft_buffer[sample].re = 0.0f;
            fft_buffer[sample].im = 0.0f;
        }

        mic16_fft4096_xilinx(fft_buffer, 0);

    cache_selected_bins_loop:
        for (unsigned int bin = 0U; bin < MIC16_TF_FRONTEND_BIN_COUNT; ++bin) {
#pragma HLS PIPELINE II=1
            ddr_arena[workspace_word_offset + MIC16_TF_FRONTEND_SPECTRUM_OFFSET
                + channel * MIC16_TF_FRONTEND_BIN_COUNT + bin] =
                pack_complex_f32(fft_buffer[MIC16_TF_FRONTEND_FIRST_BIN + bin]);
        }
    }

phat_pair_loop:
    for (unsigned int pair = 0U; pair < MIC16_HLS_PAIR_COUNT; ++pair) {
        const unsigned int channel_a = MIC16_HLS_PAIRS[pair][0];
        const unsigned int channel_b = MIC16_HLS_PAIRS[pair][1];

    load_pair_spectrum_a_loop:
        for (unsigned int bin = 0U; bin < MIC16_TF_FRONTEND_BIN_COUNT; ++bin) {
#pragma HLS PIPELINE II=1
            pair_spectrum_a[bin] = unpack_complex_f32(
                ddr_arena[workspace_word_offset + MIC16_TF_FRONTEND_SPECTRUM_OFFSET
                    + channel_a * MIC16_TF_FRONTEND_BIN_COUNT + bin]);
        }
    load_pair_spectrum_b_loop:
        for (unsigned int bin = 0U; bin < MIC16_TF_FRONTEND_BIN_COUNT; ++bin) {
#pragma HLS PIPELINE II=1
            pair_spectrum_b[bin] = unpack_complex_f32(
                ddr_arena[workspace_word_offset + MIC16_TF_FRONTEND_SPECTRUM_OFFSET
                    + channel_b * MIC16_TF_FRONTEND_BIN_COUNT + bin]);
        }

    phat_pair_bin_loop:
        for (unsigned int bin = 0U; bin < MIC16_TF_FRONTEND_BIN_COUNT; ++bin) {
#pragma HLS PIPELINE II=1
            const mic16_complex_t xa = pair_spectrum_a[bin];
            const mic16_complex_t xb = pair_spectrum_b[bin];
            const float product_real = xa.re * xb.re + xa.im * xb.im;
            const float product_imag = xa.im * xb.re - xa.re * xb.im;
            const float magnitude =
                sqrtf(product_real * product_real + product_imag * product_imag);
            float normalized_real = 0.0f;
            float normalized_imag = 0.0f;
            if (magnitude > 1.0e-8f) {
                normalized_real = product_real / magnitude;
                normalized_imag = product_imag / magnitude;
            }
            ddr_arena[workspace_word_offset + MIC16_TF_FRONTEND_PAIR_SCRATCH_OFFSET
                + pair * MIC16_TF_FRONTEND_BIN_COUNT + bin] =
                pack_q2_24(normalized_real, normalized_imag);
        }
    }

transpose_tile_loop:
    for (unsigned int base_bin = 0U;
         base_bin < MIC16_TF_FRONTEND_BIN_COUNT;
         base_bin += MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS) {
        const unsigned int remaining = MIC16_TF_FRONTEND_BIN_COUNT - base_bin;
        const unsigned int tile_bins =
            (remaining < MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS)
                ? remaining
                : MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS;

    transpose_load_pair_loop:
        for (unsigned int pair = 0U; pair < MIC16_HLS_PAIR_COUNT; ++pair) {
        transpose_load_bin_loop:
            for (unsigned int offset = 0U;
                 offset < MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS;
                 ++offset) {
#pragma HLS PIPELINE II=1
                if (offset < tile_bins) {
                    transpose_tile[pair * MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS + offset] =
                        ddr_arena[workspace_word_offset + MIC16_TF_FRONTEND_PAIR_SCRATCH_OFFSET
                            + pair * MIC16_TF_FRONTEND_BIN_COUNT + base_bin + offset];
                }
            }
        }

    transpose_write_bin_loop:
        for (unsigned int offset = 0U;
             offset < MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS;
             ++offset) {
        transpose_write_pair_loop:
            for (unsigned int pair = 0U; pair < MIC16_HLS_PAIR_COUNT; ++pair) {
#pragma HLS PIPELINE II=1
                if (offset < tile_bins) {
                    ddr_arena[workspace_word_offset + MIC16_TF_FRONTEND_OUTPUT_OFFSET
                        + (base_bin + offset) * MIC16_HLS_PAIR_COUNT + pair] =
                        transpose_tile[
                            pair * MIC16_TF_FRONTEND_TRANSPOSE_TILE_BINS + offset];
                }
            }
        }
    }
}
