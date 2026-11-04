#ifndef MIC16_FFT64_RANGE_GUARD_H
#define MIC16_FFT64_RANGE_GUARD_H
#include <stdint.h>

/* 只判断当前表和输入是否完全避开厂商FP64非正规数差异；不近似或缩放输入。
 * prepare由初始化调用，scratch需4096个int16_t；输入分析可随32复数拷贝推进。
 * 每次装载系数必须调用coefficient_accepts；表可以在请求间重建，不能在请求中修改。
 * 重建后格点变细则回CPU，或在无借用时重新prepare；不靠地址/哈希猜内容未变。
 * 返回0可送PL；2需走原CPU路径；负值仅表示调用参数无效。 */
typedef struct {
    unsigned magic,n,table_length,logarithm;
    int minimum_quantum;
    const double *twiddle_real,*twiddle_imaginary;
    int16_t coefficient_quantum[2048];
} m16_fft64_range_guard;
typedef struct {int minimum_quantum;unsigned oversized,nonfinite;} m16_fft64_input_range;

int m16_fft64_range_prepare(m16_fft64_range_guard *,const double *,const double *,unsigned,unsigned,int16_t *);
void m16_fft64_range_begin(m16_fft64_input_range *);
void m16_fft64_range_observe(m16_fft64_input_range *,uint64_t);
int m16_fft64_range_coefficient_accepts(const m16_fft64_range_guard *,unsigned,uint64_t,uint64_t);
int m16_fft64_range_accepts(const m16_fft64_range_guard *,const m16_fft64_input_range *,unsigned);
#endif
