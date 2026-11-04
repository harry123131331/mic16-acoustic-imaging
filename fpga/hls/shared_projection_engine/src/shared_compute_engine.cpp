#include "shared_projection_engine.h"
#if M16_PROJECTION_VERSION != 6u
#error 本源码只允许匹配V6协议的共享计算硬件
#endif

#include "physical_projection_exact.h"
#include "fir_shared.h"
#include "resample_coefficients_rom.h"
#include <ap_int.h>
// 仅在原始16位维度核验后写入窄字段；终止计数值也必须能表示，禁止溢出回绕。
struct Job {
    uint64_t tag;
    unsigned obs,coef,out,report,no,nc,nr;
    ap_uint<3> mode;
    ap_uint<13> fft_length,fft_table;
    ap_uint<1> fft_inverse;
    ap_uint<6> frames;
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

/* 模式互斥：投影用前两bank存观测、后两bank交错存系数；FFT复用同四bank。
 * 因此不会把两套大工作RAM简单相加。所有权由统一描述符运行器持有。 */
static uint64_t &observation_at(uint64_t (&s)[4][2048],unsigned i){
#pragma HLS INLINE
    return s[i>>11][i&2047u];
}
static uint64_t &coefficient_at(uint64_t (&s)[4][2048],unsigned i){
#pragma HLS INLINE
    return s[2u+(i&1u)][i>>1];
}
static int statistics(uint64_t *arena,const Job &j,uint64_t (&shared)[4][2048],float (&work)[512]){
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
            observation_at(shared,i)=word;
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
                coefficient_at(shared,i)=word;
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
                    float term=product_sum(observation_at(shared,(t*8+b)*32+p),coefficient_at(shared,(b*32+p)*4+c),part);
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

static int physical(uint64_t *arena,const Job &j,uint64_t (&shared)[4][2048],double (&accum)[2][160]){
#pragma HLS INLINE
    ap_uint<6> bins=j.bins;ap_uint<5> candidates=j.count;
    ap_uint<8> contexts=(j.frames<3?3u:(unsigned)j.frames)*8u;
    unsigned invalid=0;
clear_physical_frame_contexts:
    for(ap_uint<8> k=0;k<160;k++){
#pragma HLS PIPELINE II=1
        accum[0][k]=0.;accum[1][k]=0.;
    }
    /* 四通道对一块，只需20*4*34字，比原观测RAM容量小。
     * 同一系数供完整20帧复用，避免原3帧一任务重复读取7次。 */
    for(ap_uint<6> block=0;block<32;block+=4){
        for(ap_uint<5> f=0;f<j.frames;f++)for(ap_uint<3> p=0;p<4;p++){
load_physical_pair_tile:
            for(ap_uint<6> b=0;b<bins;b++){
#pragma HLS PIPELINE II=1
                uint64_t v=arena[j.obs+((f*32+block+p)*bins+b)];
                observation_at(shared,(f*4+p)*bins+b)=v;invalid|=m16_physical_bad_word(v);
            }
        }
        if(invalid)return -2;
        for(ap_uint<3> pair=0;pair<4;pair++){
load_physical_reused_coefficients:
            for(ap_uint<10> index=0;index<bins*16;index++){
#pragma HLS PIPELINE II=1
                unsigned b=index>>4,c=index&15u;
                uint64_t v=c<candidates?arena[j.coef+(((block+pair)*bins+b)*candidates+c)]:0;
                coefficient_at(shared,index)=v;invalid|=m16_physical_bad_word(v);
            }
            if(invalid)return -2;
            for(ap_uint<6> b=0;b<bins;b++){
physical_two_lane_contexts:
                for(ap_uint<8> k=0;k<contexts;k++){
#pragma HLS PIPELINE II=1
                    /* 每条通道至少24个独立累加器，反馈相加延迟小于此距离。
                     * 两个bank分别每拍一次读/写，不伪造存储端口。 */
#pragma HLS DEPENDENCE variable=accum inter RAW distance=24 true
#pragma HLS DEPENDENCE variable=accum inter WAR distance=24 true
#pragma HLS DEPENDENCE variable=accum inter WAW distance=24 true
                    unsigned frame=k>>3,base=(k&7u)*2u;
                    uint64_t observation=frame<j.frames?observation_at(shared,(frame*4+pair)*bins+b):0;
                    for(unsigned lane=0;lane<2;lane++){
#pragma HLS UNROLL
                        unsigned c=base+lane;
                        if(frame<j.frames&&c<candidates){
                            double term=m16_physical_term(observation,coefficient_at(shared,b*16+c));
                            accum[lane][k]=accum[lane][k]+term;
                        }
                    }
                }
            }
        }
    }
write_physical_frame_contexts:
    for(ap_uint<9> context=0;context<320;context++){
#pragma HLS PIPELINE II=1
        unsigned frame=context>>4,c=context&15u;
        if(frame<j.frames&&c<candidates)arena[j.out+frame*candidates+c]=m16_physical_bits(accum[c&1u][context>>1]);
    }
    return 0;
}



static unsigned fft_parity(unsigned x){
#pragma HLS INLINE
    x^=x>>8;x^=x>>4;x^=x>>2;x^=x>>1;return x&1u;
}
static unsigned fft_reversed(unsigned x,unsigned logn){
#pragma HLS INLINE
    ap_uint<12> v=x;return (unsigned)v.reverse()>>(12-logn);
}
static double negated(double x){
#pragma HLS INLINE
    return m16_physical_double(m16_physical_bits(x)^UINT64_C(0x8000000000000000));
}
static ap_uint<21> fft_address(ap_uint<21> base,ap_uint<13> index,ap_uint<1> lane){
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
// 寄存两级字地址，避免32位移位/两次相加与AXI请求组合成一条长路径。
#pragma HLS LATENCY min=2 max=2
    return base+((ap_uint<21>)index<<1)+lane;
}
static int fft_shared(uint64_t *arena,const Job &j,uint64_t (&shared)[4][2048],
                      double (&wr)[2048],double (&wi)[2048]){
#pragma HLS INLINE
    ap_uint<13> n=j.fft_length,table=j.fft_table;
    unsigned logarithm=n==4096?12u:n==2048?11u:n==512?9u:8u,invalid=0;
unified_fft_twiddle_load:
    for(ap_uint<12> i=0;i<(table>>1);i++){
#pragma HLS PIPELINE II=2
        uint64_t a=arena[fft_address(j.coef,i,0)],b=arena[fft_address(j.coef,i,1)];
        invalid|=((a&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000))||
                 ((b&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000));
        wr[i]=m16_physical_double(a);wi[i]=m16_physical_double(b);
    }
    if(invalid)return -2;
unified_fft_reverse_load:
    for(ap_uint<13> i=0;i<n;i++){
#pragma HLS PIPELINE II=2
        uint64_t a=arena[fft_address(j.obs,i,0)],b=arena[fft_address(j.obs,i,1)];
        invalid|=((a&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000))||
                 ((b&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000));
        unsigned index=fft_reversed(i,logarithm),bank=fft_parity(index),at=index>>1;
        shared[bank][at]=a;shared[2u+bank][at]=b;
    }
    if(invalid)return -2;
    fft_stages:
    for(ap_uint<4> stage=1;stage<=logarithm;stage++){
// 每级结束必须排空；禁止工具将不同级与当前级蝶形一起展开成一个流水循环。
#pragma HLS LOOP_FLATTEN off
        unsigned half=1u<<(stage-1u),mask=half-1u,stride=(unsigned)table>>stage;
unified_fft_butterflies:
        for(ap_uint<12> i=0;i<(n>>1);i++){
#pragma HLS PIPELINE II=1
// 同一级蝶形地址互斥，级间排空；全域地址证明及数值检查见本核测试。
#pragma HLS DEPENDENCE variable=shared inter false
            unsigned k=i&mask,a=((unsigned)i<<1)-k,b=a+half,q=k*stride,bank=fft_parity(a),aa=a>>1,bb=b>>1;
            double ar=m16_physical_double(shared[bank][aa]),ai=m16_physical_double(shared[2u+bank][aa]);
            double br=m16_physical_double(shared[bank^1u][bb]),bi=m16_physical_double(shared[2u+(bank^1u)][bb]);
            double wre=wr[q],wim=j.fft_inverse?negated(wi[q]):wi[q];
            // 用同一FP64加法运算类型表达精确减法，使投影与FFT能共享运算器。
            double re=wre*br+negated(wim*bi),im=wre*bi+wim*br;
            shared[bank][aa]=m16_physical_bits(ar+re);shared[2u+bank][aa]=m16_physical_bits(ai+im);
            shared[bank^1u][bb]=m16_physical_bits(ar+negated(re));shared[2u+(bank^1u)][bb]=m16_physical_bits(ai+negated(im));
        }
    }
    const double scale=n==4096?1./4096.:n==2048?1./2048.:n==512?1./512.:1./256.;
unified_fft_output:
    for(ap_uint<13> i=0;i<n;i++){
#pragma HLS PIPELINE II=2
        unsigned bank=fft_parity(i),at=i>>1;
        double a=m16_physical_double(shared[bank][at]),b=m16_physical_double(shared[2u+bank][at]);
        if(j.fft_inverse){a=a*scale;b=b*scale;}
        uint64_t r=m16_physical_bits(a),im=m16_physical_bits(b);
        invalid|=((r&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000))||
                 ((im&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000));
        arena[fft_address(j.out,i,0)]=r;arena[fft_address(j.out,i,1)]=im;
    }
    return invalid?-3:0;
}
/* 时间地址只需24位。分拍移位相加实现625倍，不给低频控制复制DSP乘法器。 */
static ap_uint<24> fir_time(ap_uint<15> start,ap_uint<6> offset){
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
#pragma HLS LATENCY min=3 max=3
    ap_uint<15> a=start+offset+10u;
    ap_uint<24> positive=((ap_uint<24>)a<<9)+((ap_uint<24>)a<<7);
    ap_uint<24> negative=((ap_uint<24>)a<<4)-a;
    return positive-negative;
}
/* FIR与原统计、物理精算、FP64 FFT互斥使用同一乘加器和四组工作RAM。
 * 冻结对称系数只存前6251项ROM。ROM之外不增加整窗缓存，保留每个输出的k升序。
 * 模式5：frames=输出点1..32，bins=1或16通道，count=输入点1..128。
 * coef两字分别为输出起点/输入起点、原输入总点数/保留0；输入已转换为FP64。
 * 只加载并验证125点左右的小块；归一化、门限及模型不在此核改动。 */
static int fir_shared(uint64_t *arena,const Job &j,uint64_t (&shared)[4][2048]){
#pragma HLS INLINE
    uint64_t meta=arena[j.coef],size=arena[j.coef+1];
    unsigned start=(uint32_t)meta,base=(uint32_t)(meta>>32),n=(uint32_t)size;
    unsigned outputs=j.frames,channels=j.bins,inputs=j.count;
    if((size>>32)||!n||n>45680u||start>18710u||base>n||inputs>n-base)return -1;
    /* end<=ceil(n*256/625) 等价于 (end-1)*625<n*256，避免常数除法硬件。 */
    if((unsigned)fir_time(start,outputs-1u)-6250u>=(n<<8))return -1;
    ap_uint<7> first[32],taps[32];ap_uint<14> phase[32];
    unsigned first_low=0,last_high=0;
fir_plan_rows:
    for(ap_uint<6> row=0;row<outputs;row++){
        unsigned time=(unsigned)fir_time(start,row);
        unsigned low=time<12500u?0u:(time-12500u+255u)>>8;
        unsigned high=time>>8;if(high>=n)high=n-1u;
        if(low>high||low<base||high>=base+inputs)return -1;
        if(row==0)first_low=low;last_high=high;
        first[row]=low-base;taps[row]=high-low+1u;phase[row]=time-low*256u;
    }
    if(first_low!=base||last_high+1u!=base+inputs)return -1;
    unsigned invalid=0;
fir_load_inputs:
    for(ap_uint<12> i=0;i<j.no;i++){
#pragma HLS PIPELINE II=1
        uint64_t word=arena[j.obs+i];invalid|=!m16_fir_safe_word(word);
        shared[(unsigned)i&1u][(unsigned)i>>1]=word;
    }
    if(invalid)return -2;
fir_clear_outputs:
    for(ap_uint<9> k=0;k<256;k++){
#pragma HLS PIPELINE II=1
        shared[2][k]=0;shared[3][k]=0;
    }
    ap_uint<9> groups=(j.nr+1u)>>1;
    /* 不足24个上下文时填空拍；各抽头层排空，内层地址一一对应，无跨层伪依赖。 */
    if(groups<24u)groups=24u;
fir_taps:
    for(ap_uint<6> tap=0;tap<49;tap++){
#pragma HLS LOOP_FLATTEN off
fir_two_lane_contexts:
        for(ap_uint<9> k=0;k<groups;k++){
#pragma HLS PIPELINE II=1
/* 同一抽头层的每个输出仅访问一次；抽头层间由LOOP_FLATTEN off排空。 */
#pragma HLS DEPENDENCE variable=shared inter false
            for(unsigned lane=0;lane<2;lane++){
#pragma HLS UNROLL
                unsigned context=(unsigned)k*2u+lane;
                unsigned row=channels==16u?context>>4:context;
                unsigned channel=channels==16u?context&15u:0u;
                if(context<j.nr&&tap<taps[row]){
                    unsigned position=(unsigned)first[row]+tap;
                    unsigned index=(channels==16u?position<<4:position)+channel;
                    unsigned h=(unsigned)phase[row]-(unsigned)tap*256u;
                    unsigned mirror=h>6250u?12500u-h:h;
                    double x=m16_physical_double(shared[index&1u][index>>1]);
                    double coefficient=m16_physical_double(m16_fir_coefficient_bits(mirror));
                    double product=x*coefficient;
                    double previous=m16_physical_double(shared[2u+lane][k]);
                    shared[2u+lane][k]=m16_physical_bits(previous+product);
                }
            }
        }
    }
fir_write_outputs:
    for(ap_uint<10> i=0;i<j.nr;i++){
#pragma HLS PIPELINE II=1
        arena[j.out+i]=shared[2u+((unsigned)i&1u)][(unsigned)i>>1];
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
#pragma HLS ALLOCATION operation instances=dadd limit=6
#pragma HLS ALLOCATION function instances=m16_physical_product_bits limit=4
// 完整跨任务检查由提交前的PS驱动执行，PL只保留基本硬件边界核验。
#pragma HLS ALLOCATION function instances=range limit=1
#pragma HLS ALLOCATION function instances=fft_address limit=2
#pragma HLS ALLOCATION operation instances=dmul limit=4
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
        if(!frames||!bins||!count)return -1;
        if(mode==1){
            if(frames>4||bins>256||count>16)return -1;
        }else if(mode==2){return -1;}else if(mode==3){
            if(frames>20||bins>34||count>16)return -1;
        }else if(mode==4){
            if((frames!=256&&frames!=512&&frames!=2048&&frames!=4096)||
               (bins!=512&&bins!=4096)||bins<frames||count>2)return -1;
            j.fft_length=frames;j.fft_table=bins;j.fft_inverse=count-1;
        }else if(mode==5){
            if(frames>32u||(bins!=1u&&bins!=16u)||count>128u||nr!=(bins==16u?frames<<4:frames)||
               (uint32_t)spans!=(bins==16u?count<<4:count)||(uint32_t)(spans>>32)!=2u)return -1;
        }else return -1;
        j.mode=mode;j.frames=frames;j.bins=bins;j.count=count;
        if(!range(j.obs,j.no,arena_words)||!range(j.coef,j.nc,arena_words)||
           !range(j.out,j.nr,arena_words)||!range(j.report,2,arena_words)||!j.no||!j.nc||!j.nr)return -1;
    }
    uint64_t shared[4][2048];double wr[2048],wi[2048];float work[512];double accum[2][160];
#pragma HLS BIND_STORAGE variable=shared type=ram_t2p impl=bram
#pragma HLS ARRAY_PARTITION variable=shared complete dim=1
#pragma HLS BIND_STORAGE variable=wr type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=wi type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=work type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=accum type=ram_2p impl=bram
#pragma HLS ARRAY_PARTITION variable=accum complete dim=1
    for(unsigned i=0;i<jobs;i++){
        const Job &j=commands[i];int rc;
        if(j.mode==1)rc=statistics(arena,j,shared,work);
        
        else if(j.mode==3)rc=physical(arena,j,shared,accum);
        else if(j.mode==4)rc=fft_shared(arena,j,shared,wr,wi);
        else rc=fir_shared(arena,j,shared);
        arena[j.report+1]=(uint32_t)rc|((uint64_t)(rc?0:j.nr)<<32);
        arena[j.report]=j.tag;
        if(rc)return rc;
    }
    return (int)jobs;
}
