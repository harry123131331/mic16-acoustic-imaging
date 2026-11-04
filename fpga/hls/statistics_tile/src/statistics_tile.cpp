#include "statistics_tile.h"
#ifndef M16_TILE_SERIAL_COMPONENTS
#define M16_TILE_SERIAL_COMPONENTS 0
#endif
/* 位级处理显式保留符号零，按已核验NEON语义清除非正规数。 */
static float clean(float value) {
#pragma HLS INLINE
    union {float f;uint32_t u;} v;v.f=value;
    if((v.u&0x7f800000u)==0)v.u&=0x80000000u;
    return v.f;
}
static float component(uint64_t word,unsigned imaginary) {
#pragma HLS INLINE
    union {float f;uint32_t u;} v;v.u=(uint32_t)(imaginary?word>>32:word);
    return clean(v.f);
}
static uint64_t pack(float re,float im) {
#pragma HLS INLINE
    union {float f;uint32_t u;} a,b;a.f=re;b.f=im;
    return (uint64_t)a.u|((uint64_t)b.u<<32);
}
static unsigned bad_component(uint64_t word) {
#pragma HLS INLINE
    /* 1.000001f是允许的原FP32分量上界；位序仅用于非负绝对值。 */
    return ((uint32_t)word&0x7fffffffu)>0x3f800008u ||
        ((uint32_t)(word>>32)&0x7fffffffu)>0x3f800008u;
}
static unsigned separate(unsigned a,unsigned na,unsigned b,unsigned nb) {
#pragma HLS INLINE
    return a<=b?na<=b-a:nb<=a-b;
}
int m16_statistics_tile(uint64_t *arena,unsigned arena_words,
    unsigned observations,unsigned coefficients,unsigned output,
    unsigned frames,unsigned bin_count,unsigned candidates) {
#pragma HLS INTERFACE m_axi port=arena offset=slave bundle=gmem0 depth=2306
#pragma HLS INTERFACE s_axilite port=arena bundle=control
#pragma HLS INTERFACE s_axilite port=arena_words bundle=control
#pragma HLS INTERFACE s_axilite port=observations bundle=control
#pragma HLS INTERFACE s_axilite port=coefficients bundle=control
#pragma HLS INTERFACE s_axilite port=output bundle=control
#pragma HLS INTERFACE s_axilite port=frames bundle=control
#pragma HLS INTERFACE s_axilite port=bin_count bundle=control
#pragma HLS INTERFACE s_axilite port=candidates bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
#pragma HLS EXPRESSION_BALANCE off
    if(!arena || !frames || frames>4 || !bin_count || bin_count>8 || !candidates || candidates>4 ||
       observations>arena_words || coefficients>arena_words || output>arena_words ||
       arena_words-observations<1024 || arena_words-coefficients<1024 || arena_words-output<256 ||
       !separate(observations,1024,coefficients,1024) ||
       !separate(observations,1024,output,256) || !separate(coefficients,1024,output,256))return -1;
    uint64_t observed[1024],signature[1024];
    unsigned invalid=0;
#if M16_TILE_SERIAL_COMPONENTS
    float sums[512];
#pragma HLS BIND_STORAGE variable=sums type=ram_2p impl=bram
#else
    float sr[128],si[128],lr[128],li[128];
#pragma HLS BIND_STORAGE variable=sr type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=si type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=lr type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=li type=ram_2p impl=bram
#endif
#pragma HLS BIND_STORAGE variable=observed type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=signature type=ram_2p impl=bram
load_observations:
    for(unsigned i=0;i<1024;i++) {
#pragma HLS PIPELINE II=1
        uint64_t word=arena[observations+i];observed[i]=word;
        if((i>>8)<frames && ((i>>5)&7u)<bin_count)invalid|=bad_component(word);
    }
load_coefficients:
    for(unsigned i=0;i<1024;i++) {
#pragma HLS PIPELINE II=1
        uint64_t word=arena[coefficients+i];signature[i]=word;
        if((i>>7)<bin_count && (i&3u)<candidates)invalid|=bad_component(word);
    }
    if(invalid)return -2;
#if M16_TILE_SERIAL_COMPONENTS
clear_shared_contexts:
    for(unsigned ctx=0;ctx<512;ctx++) {
#pragma HLS PIPELINE II=1
        sums[ctx]=0.f;
    }
    /* 实、虚部分时共用两乘两加；加法次序逐结果保持，负号通过符号位实现。 */
rotate_shared_components:
    for(unsigned flat=0;flat<8192;flat++) {
#pragma HLS PIPELINE II=1
#if defined(M16_TILE_PROVEN_DISTANCE) && M16_TILE_PROVEN_DISTANCE
        /* 8192次迭代中，每个有效地址按相同p顺序相隔256次再次访问。
         * 只声明已核验的真实距离，不删除任何读写依赖。 */
#pragma HLS DEPENDENCE variable=sums inter RAW distance=256 true
#pragma HLS DEPENDENCE variable=sums inter WAR distance=256 true
#pragma HLS DEPENDENCE variable=sums inter WAW distance=256 true
#endif
        unsigned p=flat>>8,ctx=(flat>>1)&127u,part=flat&1u,c=ctx&3u,b=(ctx>>2)&7u,t=ctx>>5;
        if(t<frames && b<bin_count && c<candidates) {
            uint64_t a=observed[(t*8+b)*32+p],s=signature[(b*32+p)*4+c];
            float re=component(a,part),im=component(a,part^1u),r=component(s,0),q=component(s,1);
            float first=clean(re*r),second=clean(im*q);
            union{float f;uint32_t u;} sign;sign.f=second;if(part)sign.u^=0x80000000u;
            float value=clean(first+sign.f);unsigned index=((p>>4)*128u+ctx)*2u+part;
            sums[index]=clean(sums[index]+value);
        }
    }
write_shared_results:
    for(unsigned index=0;index<256;index++) {
#pragma HLS PIPELINE II=1
        unsigned ctx=index>>1,part=index&1u,c=ctx&3u,b=(ctx>>2)&7u,t=ctx>>5;
        if(t<frames && b<bin_count && c<candidates)
            arena[output+index]=pack(sums[part*256u+ctx*2u],sums[part*256u+ctx*2u+1u]);
    }
#else
clear_contexts:
    for(unsigned ctx=0;ctx<128;ctx++) {
#pragma HLS PIPELINE II=1
        sr[ctx]=si[ctx]=lr[ctx]=li[ctx]=0.f;
    }
    /* 每个上下文保留原32对顺序；不同帧/频点/候选交错覆盖浮点反馈延迟。
     * 不标记虚假DEPENDENCE，不重排单个结果的加法。 */
rotate_contexts:
    for(unsigned flat=0;flat<4096;flat++) {
#pragma HLS PIPELINE II=1
        unsigned p=flat>>7,ctx=flat&127u,c=ctx&3u,b=(ctx>>2)&7u,t=ctx>>5;
        if(t<frames && b<bin_count && c<candidates) {
            uint64_t a=observed[(t*8+b)*32+p],s=signature[(b*32+p)*4+c];
            float re=component(a,0),im=component(a,1),r=component(s,0),q=component(s,1);
            float rr=clean(re*r),ii=clean(im*q),ir=clean(im*r),ri=clean(re*q);
            float cr=clean(rr+ii),ci=clean(ir-ri);
            if(p<16){sr[ctx]=clean(sr[ctx]+cr);si[ctx]=clean(si[ctx]+ci);}
            else{lr[ctx]=clean(lr[ctx]+cr);li[ctx]=clean(li[ctx]+ci);}
        }
    }
write_results:
    for(unsigned ctx=0;ctx<128;ctx++) {
#pragma HLS PIPELINE II=2
        unsigned c=ctx&3u,b=(ctx>>2)&7u,t=ctx>>5;
        if(t<frames && b<bin_count && c<candidates) {
            arena[output+ctx*2]=pack(sr[ctx],si[ctx]);
            arena[output+ctx*2+1]=pack(lr[ctx],li[ctx]);
        }
    }
#endif
    return 0;
}
