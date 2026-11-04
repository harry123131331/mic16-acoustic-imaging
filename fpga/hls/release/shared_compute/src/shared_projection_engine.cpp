#include "shared_projection_engine.h"
#include "physical_projection_exact.h"
#include <ap_int.h>
// 仅在原始16位维度核验后写入窄字段；终止计数值也必须能表示，禁止溢出回绕。
struct Job {
    uint64_t tag;
    unsigned obs,coef,out,report,no,nc,nr;
    ap_uint<2> mode;
    ap_uint<3> frames;
    ap_uint<9> bins,count;
};
static unsigned range(unsigned offset,unsigned count,unsigned words){
#pragma HLS INLINE off
// 低频检查跨两拍完成，避免返回值与批次拒收逻辑连成超过有效时钟预算的组合路径。
#pragma HLS LATENCY min=2 max=2
    return offset<=words&&count<=words-offset;
}
static unsigned bad(uint64_t word){
#pragma HLS INLINE
    return ((uint32_t)word&0x7fffffffu)>0x3f800008u||((uint32_t)(word>>32)&0x7fffffffu)>0x3f800008u;
}
static float clean(float f){
#pragma HLS INLINE
    union {float f;uint32_t u;} input,output;input.f=f;
    output.u=(input.u&0x7f800000u)?input.u:(input.u&0x80000000u);return output.f;
}
static float component(uint64_t word,unsigned part){
#pragma HLS INLINE
    union {float f;uint32_t u;} v;v.u=(uint32_t)(part?word>>32:word);return clean(v.f);
}
static uint64_t pack(float re,float im){
#pragma HLS INLINE
    union {float f;uint32_t u;} a,b;a.f=re;b.f=im;return (uint64_t)a.u|((uint64_t)b.u<<32);
}
static float product_sum(uint64_t observation,uint64_t coefficient,unsigned part){
#pragma HLS INLINE
    float re=component(observation,part),im=component(observation,part^1u);
    float first=clean(re*component(coefficient,0)),second=clean(im*component(coefficient,1));
    union {float f;uint32_t u;} input,sign;input.f=second;
    sign.u=input.u^(part?0x80000000u:0u);
    return clean(first+sign.f);
}
static int statistics(uint64_t *arena,const Job &j,uint64_t (&observed)[3264],uint64_t (&signature)[1088],float (&work)[1088]){
#pragma HLS INLINE
    ap_uint<5> count=j.count; // 统计模式已验证候选数不超过16。
    for(ap_uint<9> begin=0;begin<j.bins;begin+=8){
        unsigned bins=j.bins-begin;if(bins>8)bins=8;
        unsigned invalid=0;
load_stat_observations:
        for(ap_uint<11> i=0;i<1024;i++){
#pragma HLS PIPELINE II=1
            unsigned t=i>>8,b=(i>>5)&7u,p=i&31u;
            uint64_t word=0;if(t<j.frames&&b<bins){word=arena[j.obs+(t*j.bins+begin+b)*32+p];invalid|=bad(word);}
            observed[i]=word;
        }
        if(invalid)return -2;
        // 一个观测块在片内供全部候选组复用，不再由PS重复清洁、搬运和启动。
        for(ap_uint<5> base=0;base<count;base+=4){
            unsigned candidates=count-base;if(candidates>4)candidates=4;
load_stat_coefficients:
            for(ap_uint<11> i=0;i<1024;i++){
#pragma HLS PIPELINE II=1
                unsigned b=i>>7,p=(i>>2)&31u,c=i&3u;uint64_t word=0;
                if(b<bins&&c<candidates){word=arena[j.coef+(((base/4*j.bins+begin+b)*32+p)*4+c)];invalid|=bad(word);}
                signature[i]=word;
            }
            if(invalid)return -2;
clear_stat_contexts:
            for(ap_uint<10> i=0;i<512;i++){
#pragma HLS PIPELINE II=1
                work[i]=0.f;
            }
rotate_stat_contexts:
            for(ap_uint<14> flat=0;flat<8192;flat++){
#pragma HLS PIPELINE II=1
                // 同一累加地址再次访问严格相隔256次迭代，不删除真实依赖。
#pragma HLS DEPENDENCE variable=work inter RAW distance=256 true
#pragma HLS DEPENDENCE variable=work inter WAR distance=256 true
#pragma HLS DEPENDENCE variable=work inter WAW distance=256 true
                unsigned p=flat>>8,ctx=(flat>>1)&127u,part=flat&1u,c=ctx&3u,b=(ctx>>2)&7u,t=ctx>>5;
                if(t<j.frames&&b<bins&&c<candidates){
                    float term=product_sum(observed[(t*8+b)*32+p],signature[(b*32+p)*4+c],part);
                    unsigned index=((p>>4)*128u+ctx)*2u+part;
                    work[index]=clean(work[index]+term);
                }
            }
write_stat_contexts:
            for(ap_uint<8> ctx=0;ctx<128;ctx++){
#pragma HLS PIPELINE II=2
                unsigned c=ctx&3u,b=(ctx>>2)&7u,t=ctx>>5;
                if(t<j.frames&&b<bins&&c<candidates){
                    unsigned output=j.out+((t*j.bins+begin+b)*count+base+c)*2;
                    arena[output]=pack(work[ctx*2],work[ctx*2+1]);
                    arena[output+1]=pack(work[256+ctx*2],work[257+ctx*2]);
                }
            }
        }
    }
    return 0;
}
static int scan(uint64_t *arena,const Job &j,uint64_t (&observed)[3264],uint64_t (&signature)[1088],
                float (&work)[1088],float (&scratch)[544]){
#pragma HLS INLINE
    ap_uint<6> bins=j.bins; // 扫描模式已验证每带不超过34个频点。
    ap_uint<11> terms=bins*32;
    // 一帧观测留在片内供所有待精算方向复用；不同数值语义不共用归约顺序。
    for(ap_uint<3> t=0;t<j.frames;t++){
        unsigned invalid=0;
load_scan_observations:
        for(ap_uint<11> i=0;i<terms;i++){
#pragma HLS PIPELINE II=1
            uint64_t word=arena[j.obs+t*terms+i];observed[i]=word;invalid|=bad(word);
        }
        if(invalid)return -2;
        for(ap_uint<9> g=0;g<j.count;g++){
load_scan_coefficients:
            for(ap_uint<11> i=0;i<terms;i++){
#pragma HLS PIPELINE II=1
                uint64_t word=arena[j.coef+g*terms+i];signature[i]=word;invalid|=bad(word);
            }
            if(invalid)return -2;
scan_products:
            for(ap_uint<11> i=0;i<terms;i++){
#pragma HLS PIPELINE II=1
                work[i]=product_sum(observed[i],signature[i],0);
            }
            ap_uint<11> count=terms;unsigned source_work=1;
            while(count>1){
                ap_uint<10> pairs=count/2;
                // 每层在两个物理存储间交替，不覆盖本层仍需读取的输入。
                if(source_work){
scan_pair_tree_to_scratch:
                    for(ap_uint<10> i=0;i<pairs;i++){
#pragma HLS PIPELINE II=2
                        scratch[i]=clean(work[i*2]+work[i*2+1]);
                    }
                    if(count&1u)scratch[pairs]=work[count-1];
                }else{
scan_pair_tree_to_work:
                    for(ap_uint<10> i=0;i<pairs;i++){
#pragma HLS PIPELINE II=2
                        work[i]=clean(scratch[i*2]+scratch[i*2+1]);
                    }
                    if(count&1u)work[pairs]=scratch[count-1];
                }
                source_work^=1u;
                count=pairs+(count&1u);
            }
            arena[j.out+t*j.count+g]=pack(source_work?work[0]:scratch[0],0.f);
        }
    }
    return 0;
}
static int physical(uint64_t *arena,const Job &j,uint64_t (&observed)[3264],
                    uint64_t (&signature)[1088],double (&accum)[48]){
#pragma HLS INLINE
    ap_uint<6> bins=j.bins;ap_uint<5> candidates=j.count;
    ap_uint<11> terms=bins*32;ap_uint<12> elements=j.frames*terms;
    unsigned invalid=0;
load_physical_observations:
    for(ap_uint<12> i=0;i<elements;i++){
#pragma HLS PIPELINE II=1
        uint64_t word=arena[j.obs+i];observed[i]=word;invalid|=m16_physical_bad_word(word);
    }
    if(invalid)return -2;
clear_physical_contexts:
    for(ap_uint<6> context=0;context<48;context++){
#pragma HLS PIPELINE II=1
        accum[context]=0.;
    }
    for(ap_uint<6> pair=0;pair<32;pair++){
load_physical_coefficients:
        for(ap_uint<10> index=0;index<bins*16;index++){
#pragma HLS PIPELINE II=1
            unsigned b=index>>4,c=index&15u;
            uint64_t word=c<candidates?arena[j.coef+(pair*bins+b)*candidates+c]:0;
            signature[index]=word;invalid|=m16_physical_bad_word(word);
        }
        if(invalid)return -2;
        for(ap_uint<6> b=0;b<bins;b++){
physical_contexts:
            for(ap_uint<6> context=0;context<48;context++){
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=accum inter RAW distance=48 true
#pragma HLS DEPENDENCE variable=accum inter WAR distance=48 true
#pragma HLS DEPENDENCE variable=accum inter WAW distance=48 true
                // 固定48个独立上下文填充反馈，每个上下文内仍保持原p、b顺序。
                unsigned frame=context>>4,c=context&15u;
                if(frame<j.frames&&c<candidates){
                    double term=m16_physical_term(observed[(frame*32+pair)*bins+b],signature[b*16+c]);
                    accum[context]=accum[context]+term;
                }
            }
        }
    }
write_physical_contexts:
    for(ap_uint<6> context=0;context<48;context++){
#pragma HLS PIPELINE II=1
        unsigned frame=context>>4,c=context&15u;
        if(frame<j.frames&&c<candidates)arena[j.out+frame*candidates+c]=m16_physical_bits(accum[context]);
    }
    return 0;
}

int m16_shared_projection_engine(uint64_t *arena,unsigned arena_words,unsigned descriptors,unsigned jobs){
#pragma HLS INTERFACE m_axi port=arena offset=slave bundle=gmem0 depth=524288
#pragma HLS INTERFACE s_axilite port=arena bundle=control
#pragma HLS INTERFACE s_axilite port=arena_words bundle=control
#pragma HLS INTERFACE s_axilite port=descriptors bundle=control
#pragma HLS INTERFACE s_axilite port=jobs bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
#pragma HLS EXPRESSION_BALANCE off
#pragma HLS ALLOCATION operation instances=fmul limit=2
#pragma HLS ALLOCATION operation instances=fadd limit=2
#pragma HLS ALLOCATION operation instances=dadd limit=2
#pragma HLS ALLOCATION function instances=m16_physical_product_bits limit=2
// 完整跨任务检查由提交前的PS驱动执行，PL只保留基本硬件边界核验。
#pragma HLS ALLOCATION function instances=range limit=1
    if(!arena||!jobs||jobs>8||arena_words>M16_PROJECTION_MAX_WORDS||!range(descriptors,jobs*8,arena_words))return -1;
    Job commands[8];
    // 描述符已由PS持有独占租约并预检；基本错误仍在任何计算前拒绝。
    for(unsigned i=0;i<jobs;i++){
        uint64_t a=arena[descriptors+i*8],b=arena[descriptors+i*8+2];
        uint64_t c=arena[descriptors+i*8+3],d=arena[descriptors+i*8+4];Job &j=commands[i];
        unsigned mode=(unsigned)(a>>48);j.tag=arena[descriptors+i*8+1];
        uint64_t nr=arena[descriptors+i*8+5],spans=arena[descriptors+i*8+6];
        if((uint32_t)a!=0x4d313650u||((a>>32)&65535u)!=M16_PROJECTION_VERSION||!j.tag||(d>>48)||
           (nr>>32)||arena[descriptors+i*8+7])return -1;
        j.obs=(uint32_t)b;j.coef=(uint32_t)(b>>32);j.out=(uint32_t)c;j.report=(uint32_t)(c>>32);
        unsigned frames=(unsigned)d&65535u,bins=(unsigned)(d>>16)&65535u,count=(unsigned)(d>>32)&65535u;
        j.nr=(uint32_t)nr;j.no=(uint32_t)spans;j.nc=(uint32_t)(spans>>32);
        if(!frames||frames>4||!bins||!count)return -1;
        if(mode==1){
            if(bins>256||count>16)return -1;
        }else if(mode==2){
            if(bins>34||count>325)return -1;
        }else if(mode==3){
            if(frames>3||bins>34||count>16)return -1;
        }else return -1;
        j.mode=mode;j.frames=frames;j.bins=bins;j.count=count;
        if(!range(j.obs,j.no,arena_words)||!range(j.coef,j.nc,arena_words)||
           !range(j.out,j.nr,arena_words)||!range(j.report,2,arena_words)||!j.no||!j.nc||!j.nr)return -1;
    }
    uint64_t observed[3264],signature[1088];float work[1088],scratch[544];double accum[48];
#pragma HLS BIND_STORAGE variable=observed type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=signature type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=work type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=scratch type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=accum type=ram_2p impl=bram
    for(unsigned i=0;i<jobs;i++){
        const Job &j=commands[i];int rc;
        if(j.mode==1)rc=statistics(arena,j,observed,signature,work);
        else if(j.mode==2)rc=scan(arena,j,observed,signature,work,scratch);
        else rc=physical(arena,j,observed,signature,accum);
        arena[j.report+1]=(uint32_t)rc|((uint64_t)(rc?0:j.nr)<<32);
        arena[j.report]=j.tag;
        if(rc)return rc;
    }
    return (int)jobs;
}
