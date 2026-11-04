#ifndef MIC16_FFT_ENGINE_HLS_H
#define MIC16_FFT_ENGINE_HLS_H

#include "mic16_ssl_hls_config.h"

/*
 * Official Xilinx FFT backend used by synthesis-oriented HLS solutions.
 * The reference CSim path keeps the independent radix-2 implementation so
 * numerical changes introduced by the FFT IP remain visible in Golden tests.
 */
void mic16_fft4096_xilinx(mic16_complex_t data[MIC16_HLS_FFT_LEN], int inverse);

#endif
