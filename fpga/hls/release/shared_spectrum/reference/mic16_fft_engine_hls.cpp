#include "mic16_fft_engine_hls.h"

#include "hls_fft.h"
#include <stdint.h>

struct mic16_fft_ip_config : hls::ip_fft::params_t {
    static const unsigned input_width = 32;
    static const unsigned output_width = 32;
    static const unsigned status_width = 8;
    static const unsigned config_width = 32;
    static const unsigned max_nfft = 12;
    static const unsigned arch_opt = hls::ip_fft::radix_2_burst_io;
    static const unsigned ordering_opt = hls::ip_fft::natural_order;
    static const unsigned scaling_opt = hls::ip_fft::scaled;
    static const unsigned phase_factor_width = 24;
    static const bool ovflo = true;
};

typedef hls::ip_fft::config_t<mic16_fft_ip_config> mic16_fft_config_t;
typedef hls::ip_fft::status_t<mic16_fft_ip_config> mic16_fft_status_t;
typedef hls::x_complex<float> mic16_fft_data_t;

#ifdef AESL_SYN
union mic16_float_bits_t {
    float f;
    unsigned int u;
};

static void mic16_pack_fft_input(
    const mic16_complex_t data[MIC16_HLS_FFT_LEN],
    uint64_t packed[MIC16_HLS_FFT_LEN])
{
#pragma HLS INLINE off
pack_input:
    for (unsigned int i = 0U; i < MIC16_HLS_FFT_LEN; i++) {
#pragma HLS PIPELINE II=1
        mic16_float_bits_t re_bits;
        mic16_float_bits_t im_bits;
        re_bits.f = data[i].re;
        im_bits.f = data[i].im;
        packed[i] = ((uint64_t)im_bits.u << 32) | (uint64_t)re_bits.u;
    }
}

static void mic16_unpack_fft_output(
    mic16_complex_t data[MIC16_HLS_FFT_LEN],
    uint64_t packed[MIC16_HLS_FFT_LEN],
    int inverse)
{
#pragma HLS INLINE off
    const float scale = inverse ? (1.0f / (float)MIC16_HLS_FFT_LEN) : 1.0f;
unpack_output:
    for (unsigned int i = 0U; i < MIC16_HLS_FFT_LEN; i++) {
#pragma HLS PIPELINE II=1
        const uint64_t value = packed[i];
        mic16_float_bits_t re_bits;
        mic16_float_bits_t im_bits;
        re_bits.u = (unsigned int)value;
        im_bits.u = (unsigned int)(value >> 32);
        data[i].re = re_bits.f * scale;
        data[i].im = im_bits.f * scale;
    }
}

static void mic16_write_fft_config(
    hls::stream<mic16_fft_config_t> &config_stream,
    int inverse)
{
#pragma HLS INLINE off
    mic16_fft_config_t config;
    /* Xilinx FFT: direction=1 is forward and direction=0 is inverse. */
    config.setDir(inverse ? false : true);
    /* Native floating-point FFT ignores SCALE_SCH; normalize the IFFT later. */
    config.setSch(0U);
    config_stream.write(config);
}

static void mic16_read_fft_status(
    hls::stream<mic16_fft_status_t> &status_stream)
{
#pragma HLS INLINE off
    const mic16_fft_status_t status = status_stream.read();
    (void)status;
}

#endif

void mic16_fft4096_xilinx(mic16_complex_t data[MIC16_HLS_FFT_LEN], int inverse)
{
#pragma HLS INLINE off
#ifdef AESL_SYN
    uint64_t fft_in_packed[MIC16_HLS_FFT_LEN];
    uint64_t fft_out_packed[MIC16_HLS_FFT_LEN];
    hls::stream<mic16_fft_config_t> config_stream;
    hls::stream<mic16_fft_status_t> status_stream;
#pragma HLS STREAM variable=fft_in_packed depth=4096
#pragma HLS STREAM variable=fft_out_packed depth=4096
#pragma HLS STREAM variable=config_stream depth=2
#pragma HLS STREAM variable=status_stream depth=2

    mic16_pack_fft_input(data, fft_in_packed);
    mic16_write_fft_config(config_stream, inverse);
    hls::fft_syn<mic16_fft_ip_config,
                 32,
                 32,
                 MIC16_HLS_FFT_LEN,
                 1,
                 hls::ip_fft::floating_point>(
        fft_in_packed,
        fft_out_packed,
        status_stream,
        config_stream);
    mic16_unpack_fft_output(data, fft_out_packed, inverse);
    mic16_read_fft_status(status_stream);
#else
    static mic16_fft_data_t fft_in[MIC16_HLS_FFT_LEN];
    static mic16_fft_data_t fft_out[MIC16_HLS_FFT_LEN];
    mic16_fft_config_t config;
    mic16_fft_status_t status;

copy_in:
    for (unsigned int i = 0U; i < MIC16_HLS_FFT_LEN; i++) {
#pragma HLS PIPELINE II=1
        fft_in[i] = mic16_fft_data_t(data[i].re, data[i].im);
    }

    /* Xilinx FFT: direction=1 is forward and direction=0 is inverse. */
    config.setDir(inverse ? false : true);
    /* Native floating-point FFT ignores SCALE_SCH; normalize the IFFT below. */
    config.setSch(0U);
    hls::fft<mic16_fft_ip_config>(fft_in, fft_out, &status, &config);

copy_out:
    for (unsigned int i = 0U; i < MIC16_HLS_FFT_LEN; i++) {
#pragma HLS PIPELINE II=1
        const float scale = inverse ? (1.0f / (float)MIC16_HLS_FFT_LEN) : 1.0f;
        data[i].re = fft_out[i].real() * scale;
        data[i].im = fft_out[i].imag() * scale;
    }
#endif
}
