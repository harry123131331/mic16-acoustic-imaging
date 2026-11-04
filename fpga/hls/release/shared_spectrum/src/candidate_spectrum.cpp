/* 当前常驻链只提交模式4；旧模式0/1仍保存在原核和历史BIT中。
 * 有效模式4的加载、补零、厂商FP32 FFT及807频点写出逐句保持。 */
#include "shared_spectrum.h"
#include "mic16_tf_phat_frontend_hls_q2_24.h"
#include "mic16_fft_engine_hls.h"
static mic16_complex_t shared_fft_buffer[MIC16_HLS_FFT_LEN];
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
int m16_sp_shared(ap_uint<64> *ddr_arena,unsigned pcm_word_offset,unsigned workspace_word_offset,
                  unsigned frame_samples,unsigned mode,unsigned active_channels){
#pragma HLS INTERFACE m_axi port=ddr_arena offset=slave bundle=gmem0 depth=80944
#pragma HLS INTERFACE s_axilite port=ddr_arena bundle=control
#pragma HLS INTERFACE s_axilite port=pcm_word_offset bundle=control
#pragma HLS INTERFACE s_axilite port=workspace_word_offset bundle=control
#pragma HLS INTERFACE s_axilite port=frame_samples bundle=control
#pragma HLS INTERFACE s_axilite port=mode bundle=control
#pragma HLS INTERFACE s_axilite port=active_channels bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
#pragma HLS ALLOCATION instances=mic16_fft4096_xilinx limit=1 function
 if(mode!=4 || frame_samples!=2048 || !active_channels || active_channels>16)return -1;
candidate_channel:
 for(unsigned c=0;c<active_channels;c++){
candidate_contiguous_load:
   for(unsigned word_index=0;word_index<1024;word_index++){
#pragma HLS PIPELINE II=1
    ap_uint<64> word=ddr_arena[pcm_word_offset+c*1024+word_index];
    mic16_tf_frontend_float_bits_t low,high;low.u=(unsigned)word.range(31,0);high.u=(unsigned)word.range(63,32);
    shared_fft_buffer[2*word_index].re=low.f;shared_fft_buffer[2*word_index].im=0.f;
    shared_fft_buffer[2*word_index+1].re=high.f;shared_fft_buffer[2*word_index+1].im=0.f;
   }
candidate_pad:
  for(unsigned i=2048;i<4096;i++){
#pragma HLS PIPELINE II=1
   shared_fft_buffer[i].re=shared_fft_buffer[i].im=0.f;
  }
  mic16_fft4096_xilinx(shared_fft_buffer,0);
candidate_store:
  for(unsigned b=0;b<807;b++){
#pragma HLS PIPELINE II=1
   ddr_arena[workspace_word_offset+c*807+b]=pack_complex_f32(shared_fft_buffer[b+32]);
  }
 }
 return 0;
}
