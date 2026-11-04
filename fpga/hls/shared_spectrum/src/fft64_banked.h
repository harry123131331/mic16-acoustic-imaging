#ifndef MIC16_FFT64_BANKED_H
#define MIC16_FFT64_BANKED_H
#include <stdint.h>
/* 双精度复数FFT候选。每点两个64位字：实部在前、虚部在后。
 * twiddle为原始PS旋转表的交错位拷贝，不在PL重新生成。
 * 仅接受256/512/2048/4096点、512或4096表；输入输出范围互斥。
 * 当前仅供离线资源/数值核验，尚未接入正式BIT或声明实时通过。 */
int m16_fft64_banked(uint64_t *arena,unsigned words,unsigned input,unsigned output,
                    unsigned twiddle,unsigned length,unsigned table_length,unsigned inverse);
#endif
