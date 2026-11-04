#include "mic16_input_guard.h"
#include <float.h>
#include <stdint.h>
typedef char require_ieee_fp32[(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128)?1:-1];
static int disjoint(const void *a,uintptr_t na,const void *b,uintptr_t nb){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!a || !b || !na || !nb || x>UINTPTR_MAX-na || y>UINTPTR_MAX-nb)return 0;
    return x+na<=y || y+nb<=x;
}
static uint32_t magnitude_bits(float value){
    union {float value;uint32_t bits;} v;v.value=value;return v.bits&0x7fffffffu;
}
int mic16_input_inspect(const float *pcm,const float *reference,mic16_input_report *out){
    mic16_input_report r={M16_INPUT_UNCHECKED,0,0,0,0xffffffffu,0xffffffffu,0};
    unsigned i,active=0;
    if(!pcm || !reference || !out || (uintptr_t)pcm%_Alignof(float) ||
        (uintptr_t)reference%_Alignof(float) || (uintptr_t)out%_Alignof(mic16_input_report) ||
        !disjoint(pcm,40960u*16u*4u,reference,8192u*16u*4u) ||
        !disjoint(out,sizeof(*out),pcm,40960u*16u*4u) ||
        !disjoint(out,sizeof(*out),reference,8192u*16u*4u))return M16_INPUT_ARGUMENT;
    /* 按IEEE位型判断，避免ARM浮点环境把非正规小量当成零；没有能量门限。 */
    for(i=0;i<40960u*16u;i++){
        uint32_t bits=magnitude_bits(pcm[i]);r.observed_checked=i+1;
        if(bits>0x3f800000u){
            r.status=M16_INPUT_INVALID_PCM;r.invalid_index=i;*out=r;return r.status;
        }
        if(bits){r.nonzero_observed++;active|=1u<<(i&15u);}
    }
    r.zero_channel_mask=65535u^active;
    for(i=0;i<8192u*16u;i++){
        uint32_t bits=magnitude_bits(reference[i]);r.reference_checked=i+1;
        if(bits>0x3f800000u){
            r.status=M16_INPUT_INVALID_REFERENCE;r.invalid_index=i;r.invalid_is_reference=1;
            *out=r;return r.status;
        }
    }
    r.status=r.nonzero_observed?M16_INPUT_OK:M16_INPUT_EXACT_ZERO;*out=r;return r.status;
}
