#ifndef M16_PHYSICAL_PROJECTION_EXACT_H
#define M16_PHYSICAL_PROJECTION_EXACT_H
#include <stdint.h>
#include <ap_int.h>
static unsigned m16_physical_bad_word(uint64_t value){
#pragma HLS INLINE
    return ((uint32_t)value&0x7fffffffu)>0x3f800008u ||
           ((uint32_t)(value>>32)&0x7fffffffu)>0x3f800008u;
}
/* 有限FP32的两个24位有效尾数相乘至多48位，能精确编码为FP64。
 * 不能在这里做FTZ；FP32非正规数仍对应正常的FP64乘积。非有限输入由调用方预检。 */
static uint64_t m16_physical_product_bits(uint32_t a,uint32_t b){
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
    unsigned ea=(a>>23)&255u,eb=(b>>23)&255u;
    ap_uint<24> ma=(a&0x7fffffu)|(ea?0x800000u:0u);
    ap_uint<24> mb=(b&0x7fffffu)|(eb?0x800000u:0u);
    ap_uint<48> product=ma*mb;
    uint64_t sign=(uint64_t)((a^b)>>31)<<63;
    if(product==0)return sign;
    unsigned k=47u-(unsigned)product.countLeadingZeros();
    uint64_t p=(uint64_t)product;
    unsigned exponent=(ea?ea:1u)+(eb?eb:1u)+k+723u;
    return sign|((uint64_t)exponent<<52)|((p-((uint64_t)1<<k))<<(52u-k));
}
static double m16_physical_double(uint64_t bits){
#pragma HLS INLINE
    union {uint64_t u;double d;} value;value.u=bits;return value.d;
}
static uint64_t m16_physical_bits(double value){
#pragma HLS INLINE
    union {uint64_t u;double d;} result;result.d=value;return result.u;
}
/* 物理响应的两个独立乘积相加，随后才与该结果的旧acc相加；禁止融合FMA。 */
static double m16_physical_term(uint64_t observation,uint64_t coefficient){
#pragma HLS INLINE
    double real=m16_physical_double(m16_physical_product_bits((uint32_t)observation,(uint32_t)coefficient));
    double imag=m16_physical_double(m16_physical_product_bits((uint32_t)(observation>>32),(uint32_t)(coefficient>>32)));
    return real+imag;
}
#endif
