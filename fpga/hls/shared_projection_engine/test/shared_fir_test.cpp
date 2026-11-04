/* 冻结完整表与独立标量原k顺序参考；同时保留原统计/物理/FFT回归。 */
#include "shared_compute_old_modes.inc"
#include "fir_shared.h"
#include "resample_coefficients_rom.h"
extern "C" double m16_fir_reference_coefficient(unsigned);
#include <vector>
#include <limits>

namespace fir_test {
static uint64_t memory[524288];
static unsigned long long checks=0,compared=0,tiles=0;
static uint64_t random_state=20261028;
static double next(){random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;
    return (double)(int32_t)random_state/2147483648.;}
static int demand(bool yes,unsigned code){checks++;if(!yes)printf("FIR_FAIL %u\n",code);return yes?0:(int)code;}
static unsigned length(unsigned n){return (n*256u+624u)/625u;}
static unsigned low(unsigned m){unsigned time=(m+10u)*625u;return time<12500u?0u:(time-12500u+255u)/256u;}
static unsigned high(unsigned m,unsigned n){unsigned v=(m+10u)*625u/256u;return v<n?v:n-1;}
static int tile(const std::vector<double>& input,unsigned n,unsigned ch,unsigned start,unsigned count,unsigned kind){
    const unsigned obs=64,meta=2200,out=2304,report=3000;
    unsigned first=low(start),last=high(start+count-1,n),samples=last-first+1;
    for(unsigned i=0;i<40000;i++)memory[i]=GUARD;
    for(unsigned i=0;i<samples*ch;i++)memory[obs+i]=dp(input[(unsigned long long)first*ch+i]);
    memory[meta]=start|((uint64_t)first<<32);memory[meta+1]=n;
    memory[0]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|(UINT64_C(5)<<48);
    memory[1]=1000+tiles;memory[2]=obs|((uint64_t)meta<<32);memory[3]=out|((uint64_t)report<<32);
    memory[4]=count|((uint64_t)ch<<16)|((uint64_t)samples<<32);memory[5]=count*ch;
    memory[6]=(uint64_t)(samples*ch)|(UINT64_C(2)<<32);memory[7]=0;
    m16_projection_job plan[8];
    if(demand(!m16_projection_validate(memory,40000,0,1,plan),10))return 10;
    if(demand(m16_shared_projection_engine(memory,40000,0,1)==1,11))return 11;
    if(demand(memory[report]==memory[1]&&memory[report+1]==((uint64_t)count*ch<<32),12))return 12;
    for(unsigned m=start;m<start+count;m++)for(unsigned c=0;c<ch;c++){
        double sum=0.;int time=(int)(m+10u)*625,k0=(time-12500+255)/256,k1=time/256;
        if(time-12500<0||k0<0)k0=0;if(k1>=(int)n)k1=(int)n-1;
        for(int k=k0;k<=k1;k++){
            int h=time-k*256;
            if(h>=0&&h<=12500)sum+=input[(unsigned)k*ch+c]*m16_fir_reference_coefficient((unsigned)h);
        }
        unsigned at=out+(m-start)*ch+c;
        if(memory[at]!=dp(sum)){
            printf("FIR_DIFFERENCE n=%u ch=%u m=%u c=%u kind=%u actual=%llx expected=%llx\n",
                n,ch,m,c,kind,(unsigned long long)memory[at],(unsigned long long)dp(sum));return 13;
        }
        compared++;
    }
    if(demand(memory[out+count*ch]==GUARD&&memory[obs+samples*ch]==GUARD&&memory[meta+2]==GUARD,14))return 14;
    /* 派生跨度、保留位、元数据和读写别名必须由真实PS验证器拒收。 */
    uint64_t original=memory[5];memory[5]++;
    if(demand(m16_projection_validate(memory,40000,0,1,plan)==-1,15))return 15;memory[5]=original;
    original=memory[meta+1];memory[meta+1]|=UINT64_C(1)<<32;
    if(demand(m16_projection_validate(memory,40000,0,1,plan)==-1,16))return 16;memory[meta+1]=original;
    memory[7]=1;
    if(demand(m16_projection_validate(memory,40000,0,1,plan)==-1,17))return 17;memory[7]=0;
    original=memory[3];memory[3]=obs|((uint64_t)report<<32);
    if(demand(m16_projection_validate(memory,40000,0,1,plan)==-1,18))return 18;memory[3]=original;
    tiles++;return 0;
}
static int run_case(unsigned n,unsigned ch,unsigned kind,bool complete){
    std::vector<double> input(n*ch);
    for(unsigned i=0;i<input.size();i++){
        double x=next();
        if(kind==0)x=(double)(float)x;
        else if(kind==1)x=(i&1)?-0.:0.;
        else if(kind==2)x=(double)(float)(x*1e-40);
        else if(kind==3)x=ldexp(x,850);
        else if(kind==4)x=(i&1)?-ldexp(1.,-909):ldexp(1.,-909);
        else if(kind==5)x=i==n*ch/2?1.:0.;
        else if(kind==6)x=sin((double)i*.037)*.8;
        input[i]=x;
        if(demand(m16_fir_safe_word(dp(x))!=0,20))return 20;
    }
    if(complete){
        for(unsigned m=0;m<length(n);m+=32){unsigned count=length(n)-m;if(count>32)count=32;
            int rc=tile(input,n,ch,m,count,kind);if(rc)return rc;}
    }else{
        unsigned starts[4]={0,length(n)/2,256,length(n)>32?length(n)-32:0};
        for(unsigned i=0;i<4;i++){unsigned m=starts[i];if(m>=length(n))continue;
            unsigned count=length(n)-m;if(count>32)count=32;int rc=tile(input,n,ch,m,count,kind);if(rc)return rc;}
    }
    return 0;
}
int check(){
    for(unsigned i=0;i<12501;i++)if(demand(m16_fir_coefficients[i<=6250?i:12500-i]==dp(m16_fir_reference_coefficient(i)),30))return 30;
    const uint64_t bad[]={UINT64_C(0x0000000000000001),UINT64_C(0x7ff0000000000000),
        UINT64_C(0x7ff8000000000000),UINT64_C(0x7830000000000001),UINT64_C(0x0720000000000001)};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)if(demand(!m16_fir_safe_word(bad[i]),31))return 31;
    const unsigned shapes[][2]={{8192,16},{40960,16},{40960,1},{45680,1},{1,1},{49,16},{1025,1}};
#ifdef M16_FIR_RTL_SMALL
    if(run_case(40960,16,0,false)||run_case(49,1,4,false))return 32;
#else
    for(unsigned i=0;i<sizeof(shapes)/sizeof(*shapes);i++)for(unsigned kind=0;kind<7;kind++){
        bool full=kind==0||i>=4;int rc=run_case(shapes[i][0],shapes[i][1],kind,full);if(rc)return rc;
    }
#endif
    /* 输入不适合硬件时必须在输出写入前拒收，不把非正规数当成0。 */
    memory[64]=1;
    memory[3000]=GUARD;memory[2304]=GUARD;
    if(demand(m16_shared_projection_engine(memory,40000,0,1)==-2&&memory[2304]==GUARD,33))return 33;
    printf("FIR_SHARED_FROZEN_EXACT_PASS tiles=%llu values=%llu checks=%llu\n",tiles,compared,checks);
    return 0;
}
}
int main(){
    int rc=fir_test::check();if(rc)return rc;
#ifndef M16_FIR_RTL_SMALL
    rc=old_modes_check();if(rc)return 50+rc;
#endif
    puts("SHARED_FIR_V6_CSIM_NUMERIC_PASS_NOT_ARM_TIMING");return 0;
}
