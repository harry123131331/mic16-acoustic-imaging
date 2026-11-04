/* 实际V6 RTL：边界FIR分块后交错运行原统计、物理精算及FP64 FFT。 */
#define M16_FIR_RTL_SMALL 1
#define main fir_boundary_check
#include "shared_fir_test.cpp"
#undef main
int main(){
    int rc=fir_boundary_check();if(rc)return rc;
    for(unsigned i=0;i<PORT_DEPTH;i++)arena[i]=GUARD;
    cursor=64;
    if(!add(0,1,1,1,1,false)||!add(1,3,1,1,1,true))return 60;
    m16_projection_job plan[8];
    if(m16_projection_validate(arena,VALID_WORDS,0,2,plan)||m16_shared_projection_engine(arena,VALID_WORDS,0,2)!=2)return 61;
    for(unsigned i=0;i<plan[0].nr;i++)if(arena[plan[0].out+i]!=fp(5.5f,4.f))return 62;
    if(arena[plan[1].out]!=((uint64_t)(1023-293)<<52))return 63;
    const unsigned n=256,table=512;
    for(unsigned i=0;i<table/2;i++){
        double angle=-2.*3.14159265358979323846264338327950288*(double)i/(double)table;
        fft_numeric::wr[i]=cos(angle);fft_numeric::wi[i]=sin(angle);
        fft_numeric::arena[16384+2*i]=dp(fft_numeric::wr[i]);fft_numeric::arena[16385+2*i]=dp(fft_numeric::wi[i]);
    }
    for(unsigned i=0;i<n;i++){
        fft_numeric::r[i]=sin(.019*(double)i)*.5;fft_numeric::im[i]=cos(.013*(double)i)*.25;
        fft_numeric::arena[2*i]=dp(fft_numeric::r[i]);fft_numeric::arena[2*i+1]=dp(fft_numeric::im[i]);
    }
    fft_numeric::original(n,table,0);
    if(fft_numeric::m16_fft64_banked(fft_numeric::arena,22000,0,8192,16384,n,table,0))return 64;
    for(unsigned i=0;i<n;i++)if(fft_numeric::arena[8192+2*i]!=dp(fft_numeric::r[i])||
                               fft_numeric::arena[8193+2*i]!=dp(fft_numeric::im[i]))return 65;
    puts("SHARED_FIR_V6_ACTUAL_RTL_AND_OLD_MODES_EXACT_PASS");return 0;
}
