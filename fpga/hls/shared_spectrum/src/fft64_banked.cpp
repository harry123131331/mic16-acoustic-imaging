#include "fft64_banked.h"
#include <ap_int.h>
static unsigned parity(unsigned x){
#pragma HLS INLINE
    x^=x>>8;x^=x>>4;x^=x>>2;x^=x>>1;return x&1u;
}
static unsigned reversed(unsigned x,unsigned bits){
#pragma HLS INLINE
    ap_uint<12> v=x;
    return (unsigned)v.reverse()>>(12-bits);
}
static double number(uint64_t x){
#pragma HLS INLINE
    union{uint64_t u;double d;}v;v.u=x;return v.d;
}
static uint64_t bits(double x){
#pragma HLS INLINE
    union{uint64_t u;double d;}v;v.d=x;return v.u;
}
static bool span(unsigned at,unsigned n,unsigned limit){
#pragma HLS INLINE
    return at<=limit&&n<=limit-at;
}
static bool apart(unsigned a,unsigned an,unsigned b,unsigned bn){
#pragma HLS INLINE
    return a<=b?an<=b-a:bn<=a-b;
}
int m16_fft64_banked(uint64_t *arena,unsigned words,unsigned input,unsigned output,
                    unsigned twiddle,unsigned length,unsigned table_length,unsigned inverse){
#pragma HLS INTERFACE m_axi port=arena offset=slave bundle=gmem0 depth=524288
#pragma HLS INTERFACE s_axilite port=arena bundle=control
#pragma HLS INTERFACE s_axilite port=words bundle=control
#pragma HLS INTERFACE s_axilite port=input bundle=control
#pragma HLS INTERFACE s_axilite port=output bundle=control
#pragma HLS INTERFACE s_axilite port=twiddle bundle=control
#pragma HLS INTERFACE s_axilite port=length bundle=control
#pragma HLS INTERFACE s_axilite port=table_length bundle=control
#pragma HLS INTERFACE s_axilite port=inverse bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
#pragma HLS EXPRESSION_BALANCE off
    if(!arena||(length!=256u&&length!=512u&&length!=2048u&&length!=4096u)||
       (table_length!=512u&&table_length!=4096u)||table_length<length||inverse>1u||
       !span(input,2u*length,words)||!span(output,2u*length,words)||!span(twiddle,table_length,words)||
       !apart(input,2u*length,output,2u*length)||!apart(input,2u*length,twiddle,table_length)||
       !apart(output,2u*length,twiddle,table_length))return -1;
    double real[2][2048],imaginary[2][2048],wr[2048],wi[2048];
#pragma HLS ARRAY_PARTITION variable=real complete dim=1
#pragma HLS ARRAY_PARTITION variable=imaginary complete dim=1
#pragma HLS BIND_STORAGE variable=real type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=imaginary type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=wr type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=wi type=ram_2p impl=bram
    unsigned logarithm=length==4096u?12u:length==2048u?11u:length==512u?9u:8u;
load_twiddles:
    for(unsigned i=0;i<table_length/2u;i++){
#pragma HLS PIPELINE II=2
        uint64_t r=arena[twiddle+2u*i],im=arena[twiddle+2u*i+1u];
        if((r&0x7fffffffffffffffull)>=0x7ff0000000000000ull||
           (im&0x7fffffffffffffffull)>=0x7ff0000000000000ull)return -2;
        wr[i]=number(r);wi[i]=number(im);
    }
load_bit_reversed:
    for(unsigned i=0;i<length;i++){
#pragma HLS PIPELINE II=2
        uint64_t r=arena[input+2u*i],im=arena[input+2u*i+1u];
        if((r&0x7fffffffffffffffull)>=0x7ff0000000000000ull||
           (im&0x7fffffffffffffffull)>=0x7ff0000000000000ull)return -2;
        unsigned logical=reversed(i,logarithm),bank=parity(logical),at=logical>>1;
        real[bank][at]=number(r);imaginary[bank][at]=number(im);
    }
    /* 任一蝶形两地址只相差一个bit，故奇偶校验bank必相反。
     * 去掉地址bit0作为行号仍一一对应；每级蝶形使用不相交地址。
     * 级之间必须排空流水。此处仅移除同一级不同蝶形的伪依赖。 */
    for(unsigned stage=1;stage<=logarithm;stage++){
        unsigned half=1u<<(stage-1u),mask=half-1u,stride=table_length>>stage;
banked_butterflies:
        for(unsigned i=0;i<length/2u;i++){
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=real inter false
#pragma HLS DEPENDENCE variable=imaginary inter false
            unsigned k=i&mask,a=(i<<1)-k,b=a+half,q=k*stride,bank=parity(a);
            unsigned aa=a>>1,bb=b>>1;
            double ar=real[bank][aa],ai=imaginary[bank][aa];
            double br=real[bank^1u][bb],bi=imaginary[bank^1u][bb];
            double wre=wr[q],wim=inverse?-wi[q]:wi[q];
            double re=wre*br-wim*bi,im=wre*bi+wim*br;
            real[bank][aa]=ar+re;imaginary[bank][aa]=ai+im;
            real[bank^1u][bb]=ar-re;imaginary[bank^1u][bb]=ai-im;
        }
    }
    const double scale=length==4096u?1./4096.:length==2048u?1./2048.:length==512u?1./512.:1./256.;
store_natural_order:
    for(unsigned i=0;i<length;i++){
#pragma HLS PIPELINE II=2
        unsigned bank=parity(i),at=i>>1;double r=real[bank][at],im=imaginary[bank][at];
        if(inverse){r=r*scale;im=im*scale;}
        if((bits(r)&0x7fffffffffffffffull)>=0x7ff0000000000000ull||
           (bits(im)&0x7fffffffffffffffull)>=0x7ff0000000000000ull)return -3;
        arena[output+2u*i]=bits(r);arena[output+2u*i+1u]=bits(im);
    }
    return 0;
}
