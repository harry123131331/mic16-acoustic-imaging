#ifndef MIC16_FIR_SHARED_H
#define MIC16_FIR_SHARED_H
#include <stdint.h>
/* 冻结表的最小非零二进制量子为2^-113。输入量子不小于2^-909，
 * 故所有非零乘积/原顺序部分和至少为正常FP64；幅度<=2^900防止溢出。
 * 本门只选择等价设备路径；不通过时调用者必须走原CPU路径，不能清零替代。 */
static unsigned m16_fir_safe_word(uint64_t word){
    uint64_t magnitude=word&UINT64_C(0x7fffffffffffffff);
    if(!magnitude)return 1;
    if(magnitude>UINT64_C(0x7830000000000000))return 0;
    unsigned exponent=(unsigned)(magnitude>>52);
    if(exponent>=166u)return 1;
    if(exponent<114u)return 0;
    return !(magnitude&((UINT64_C(1)<<(166u-exponent))-1u));
}
#endif
