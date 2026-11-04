#include "mic16_fft64_range_guard.h"
#include <limits.h>
#include <string.h>
#define MAGIC 0x4d364752u
#define ZERO_QUANTUM 30000
#define UNUSED_COEFFICIENT INT16_MAX
static uint64_t bits(double x){uint64_t w;memcpy(&w,&x,8);return w;}
static int minimum(int a,int b){return a<b?a:b;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
/* 非零有限binary64的最低置位代表其精确二进制格点，不使用对数近似。 */
static int quantum(uint64_t w){
    unsigned e=(unsigned)(w>>52)&2047u;uint64_t m=w&UINT64_C(0x000fffffffffffff);
    if(!e&&!m)return ZERO_QUANTUM;
    if(e)m|=UINT64_C(0x0010000000000000);
    return (e?(int)e-1075:-1074)+(int)__builtin_ctzll(m);
}
int m16_fft64_range_prepare(m16_fft64_range_guard *g,const double *wr,const double *wi,unsigned n,unsigned table,int16_t *scratch){
    unsigned i,j,length,logarithm=0;int drop=0;const void *regions[4];uintptr_t sizes[4];
    if(!g||!wr||!wi||!scratch||(n!=256u&&n!=512u&&n!=2048u&&n!=4096u)||
       (table!=512u&&table!=4096u)||table<n||(uintptr_t)g%_Alignof(m16_fft64_range_guard)||
       (uintptr_t)wr%8u||(uintptr_t)wi%8u||(uintptr_t)scratch%_Alignof(int16_t))return -1;
    regions[0]=g;sizes[0]=sizeof(*g);regions[1]=wr;regions[2]=wi;sizes[1]=sizes[2]=(uintptr_t)table*4u;
    regions[3]=scratch;sizes[3]=(uintptr_t)n*2u;
    for(i=0;i<4;i++)for(j=i+1;j<4;j++)if(!apart(regions[i],sizes[i],regions[j],sizes[j]))return -1;
    memset(g,0,sizeof(*g));
    for(i=0;i<2048u;i++)g->coefficient_quantum[i]=UNUSED_COEFFICIENT;
    for(i=n;i>1u;i>>=1)logarithm++;
    for(i=0;i<n;i++)scratch[i]=0;
    for(length=2u;length<=n;length<<=1){
        unsigned half=length>>1,stride=table/length,base,k;
        for(base=0;base<n;base+=length)for(k=0;k<half;k++){
            unsigned a=base+k,b=a+half,q=k*stride;
            uint64_t r=bits(wr[q])&UINT64_C(0x7fffffffffffffff),v=bits(wi[q])&UINT64_C(0x7fffffffffffffff);
            /* 每个参与运算的系数必须为零或绝对值不超过1的正规数。
             * 不支持的表只要求CPU回退，不改变原FFT如何接受/拒绝该表。 */
            if(r>UINT64_C(0x3ff0000000000000)||v>UINT64_C(0x3ff0000000000000)||
               (r&&r<UINT64_C(0x0010000000000000))||(v&&v<UINT64_C(0x0010000000000000)))return 2;
            int coefficient=minimum(quantum(r),quantum(v));
            g->coefficient_quantum[q]=(int16_t)coefficient;
            int product=coefficient==ZERO_QUANTUM?ZERO_QUANTUM:scratch[b]+coefficient;
            int result=minimum(scratch[a],product);
            scratch[a]=scratch[b]=(int16_t)result;
        }
    }
    for(i=0;i<n;i++)drop=minimum(drop,scratch[i]);
    g->magic=MAGIC;g->n=n;g->table_length=table;g->logarithm=logarithm;
    g->minimum_quantum=-1022-drop;g->twiddle_real=wr;g->twiddle_imaginary=wi;return 0;
}
int m16_fft64_range_coefficient_accepts(const m16_fft64_range_guard *g,unsigned index,uint64_t re,uint64_t im){
    uint64_t r=re&UINT64_C(0x7fffffffffffffff),v=im&UINT64_C(0x7fffffffffffffff);
    if(!g||g->magic!=MAGIC||index>=g->table_length/2u)return -1;
    /* 硬件装载阶段拒绝任意NaN/Inf；提前回CPU可保持原CPU对未用系数的处理。 */
    if(r>=UINT64_C(0x7ff0000000000000)||v>=UINT64_C(0x7ff0000000000000))return 2;
    if(g->coefficient_quantum[index]==UNUSED_COEFFICIENT)return 0;
    if(r>UINT64_C(0x3ff0000000000000)||v>UINT64_C(0x3ff0000000000000)||
       (r&&r<UINT64_C(0x0010000000000000))||(v&&v<UINT64_C(0x0010000000000000)))return 2;
    return minimum(quantum(r),quantum(v))>=g->coefficient_quantum[index]?0:2;
}
void m16_fft64_range_begin(m16_fft64_input_range *range){
    if(range){range->minimum_quantum=ZERO_QUANTUM;range->oversized=range->nonfinite=0;}
}
void m16_fft64_range_observe(m16_fft64_input_range *range,uint64_t w){
    uint64_t magnitude=w&UINT64_C(0x7fffffffffffffff);
    if(!range)return;
    if(magnitude>=UINT64_C(0x7ff0000000000000)){range->nonfinite=1;return;}
    /* |输入|<=2^900、|系数|<=1，最多12级各至多3倍，排除溢出。
     * 此界刻意保守，超界时用原CPU精确路径，不截断或饱和。 */
    if(magnitude>UINT64_C(0x7830000000000000))range->oversized=1;
    range->minimum_quantum=minimum(range->minimum_quantum,quantum(magnitude));
}
int m16_fft64_range_accepts(const m16_fft64_range_guard *g,const m16_fft64_input_range *range,unsigned inverse){
    if(!g||!range||g->magic!=MAGIC||inverse>1u)return -1;
    if(range->nonfinite||range->oversized)return 2;
    /* 逆变换最后除以2^logarithm；同样必须避开非正规输出。 */
    return range->minimum_quantum>=g->minimum_quantum+(inverse?(int)g->logarithm:0)?0:2;
}
