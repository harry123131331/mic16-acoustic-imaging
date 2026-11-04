/* 仅用于离线设备数值模型：执行当前共享频谱源码与原厂FFT C模型。
 * 输入/输出布局与模式4一致；不修改PS生产任务或原厂模型实现。 */
#include "shared_spectrum.h"
#include <stdint.h>
static ap_uint<64> memory[80944];
static uint64_t batches,transforms;
extern "C" int m16_vendor_fft_words(uint64_t *words,unsigned active){
 if(!words || !active || active>16u)return -1;
 for(unsigned i=0;i<active*1024u;i++)memory[i]=words[i];
 int rc=m16_sp_shared(memory,0,16384,2048,4,active);
 if(rc)return rc;
 for(unsigned i=0;i<active*807u;i++)words[16384u+i]=memory[16384u+i].to_uint64();
 batches++;transforms+=active;return 0;
}
extern "C" uint64_t m16_vendor_fft_metric(unsigned kind){return kind?transforms:batches;}
