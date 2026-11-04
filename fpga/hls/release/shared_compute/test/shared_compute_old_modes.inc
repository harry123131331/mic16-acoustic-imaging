/* 三种数值语义的混合批次，包括FP32非正规输入对应的正常FP64乘积。 */
#include "shared_projection_engine.h"
#include "mic16_projection_protocol.h"
#include <stdio.h>
/* 联仿包装器读取完整接口depth；后备数组必须足额，合法工作范围仍只有4万字。 */
static const unsigned PORT_DEPTH=524288,VALID_WORDS=40000;
static uint64_t arena[PORT_DEPTH];
static const uint64_t GUARD=UINT64_C(0xa73b9d64c8e21f05);
static uint64_t fp(float real,float imag){union{float f;uint32_t u;}a,b;a.f=real;b.f=imag;return a.u|((uint64_t)b.u<<32);}
static uint64_t dp(double value){union{double d;uint64_t u;}r;r.d=value;return r.u;}
static unsigned cursor=64;
static unsigned add(unsigned index,unsigned mode,unsigned frames,unsigned bins,unsigned count,bool tiny){
    unsigned no=frames*bins*32;
    unsigned nc=mode==1?((count+3)/4)*bins*32*4:count*bins*32;
    unsigned nr=mode==1?frames*bins*count*2:frames*count;
    unsigned obs=cursor;cursor+=no+8;unsigned coef=cursor;cursor+=nc+8;
    unsigned out=cursor;cursor+=nr+8;unsigned report=cursor;cursor+=2;
    if(cursor>VALID_WORDS)return 0;
    for(unsigned i=0;i<no;i++)arena[obs+i]=tiny?1:fp(.5f,.25f);
    for(unsigned i=0;i<nc;i++)arena[coef+i]=tiny?1:fp(.75f,-.125f);
    arena[index*8]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|((uint64_t)mode<<48);
    arena[index*8+1]=1001+index;
    arena[index*8+2]=obs|((uint64_t)coef<<32);arena[index*8+3]=out|((uint64_t)report<<32);
    arena[index*8+4]=frames|((uint64_t)bins<<16)|((uint64_t)count<<32);
    arena[index*8+5]=nr;arena[index*8+6]=no|((uint64_t)nc<<32);arena[index*8+7]=0;
    return 1;
}
#include <math.h>
#include <string.h>

namespace fft_numeric {
static int m16_fft64_banked(uint64_t *memory,unsigned words,unsigned input,unsigned output,unsigned twiddle,
                           unsigned length,unsigned table_length,unsigned inverse){
    // 描述符和回执固定放在所有测试数据之外，并经过实际PS验证器。
    if(length==1024||inverse>1)return -1;
    unsigned d=21000,r=21008;
    memory[d]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|(UINT64_C(4)<<48);
    memory[d+1]=9283;memory[d+2]=input|((uint64_t)twiddle<<32);memory[d+3]=output|((uint64_t)r<<32);
    memory[d+4]=length|((uint64_t)table_length<<16)|((uint64_t)(inverse+1)<<32);
    memory[d+5]=length*2;memory[d+6]=(uint64_t)(length*2)|((uint64_t)table_length<<32);memory[d+7]=0;
    m16_projection_job jobs[8];
    if(m16_projection_validate(memory,words,d,1,jobs))return -1;
    int rc=::m16_shared_projection_engine(memory,words,d,1);
    if(rc<0)return rc;
    return memory[r]==9283&&memory[r+1]==((uint64_t)length*2<<32)?0:-4;
}
/* 独立标量FP64参考与原有交换/蝶形顺序；不使用被测bank函数。 */
static uint64_t arena[22000];static double r[4096],im[4096],wr[2048],wi[2048];
static uint64_t pack(double d){uint64_t v;memcpy(&v,&d,8);return v;}
static uint64_t state=20261027;
static double sample(){state^=state<<13;state^=state>>7;state^=state<<17;return (double)(int32_t)state/2147483648.;}
static void original(unsigned n,unsigned table,unsigned inverse){
    for(unsigned i=1,j=0;i<n;i++){
        unsigned bit=n/2u;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){double a=r[i],b=im[i];r[i]=r[j];im[i]=im[j];r[j]=a;im[j]=b;}
    }
    for(unsigned len=2;len<=n;len<<=1){unsigned half=len/2;
        for(unsigned base=0;base<n;base+=len)for(unsigned k=0;k<half;k++){
            unsigned a=base+k,b=a+half,q=k*(table/len);
            double x=wr[q],y=inverse?-wi[q]:wi[q];
            double re=x*r[b]-y*im[b],ii=x*im[b]+y*r[b],ar=r[a],ai=im[a];
            r[a]=ar+re;im[a]=ai+ii;r[b]=ar-re;im[b]=ai-ii;
        }
    }
    if(inverse)for(unsigned i=0;i<n;i++){r[i]/=(double)n;im[i]/=(double)n;}
}
int check(){unsigned long long values=0;unsigned cases=0;
    for(unsigned n=256;n<=4096;n*=2){if(n==1024)continue;
        for(unsigned table=512;table<=4096;table*=8){if(table<n)continue;
            for(unsigned inverse=0;inverse<2;inverse++)for(unsigned kind=0;kind<5;kind++){
                for(unsigned i=0;i<table/2;i++){double a=-2.*3.14159265358979323846264338327950288*(double)i/(double)table;
                    wr[i]=cos(a);wi[i]=sin(a);arena[16384+2*i]=pack(wr[i]);arena[16385+2*i]=pack(wi[i]);}
                for(unsigned i=0;i<n;i++){
                    double a=sample(),b=sample();
                    if(kind==1){a*=1e-30;b*=1e-30;}else if(kind==2){a=i==17?1.:0.;b=0.;}
                    else if(kind==3){a=i&1u?-0.:0.;b=i&2u?-0.:0.;}
                    else if(kind==4){a*=1e100;b*=1e100;}
                    r[i]=a;im[i]=b;arena[2*i]=pack(a);arena[2*i+1]=pack(b);
                    arena[8192+2*i]=arena[8193+2*i]=0xbff123456789abcdull;
                }
                original(n,table,inverse);
                int rc=m16_fft64_banked(arena,22000,0,8192,16384,n,table,inverse);
                if(rc){printf("BAD_RC %u %u %u %u %d\n",n,table,inverse,kind,rc);return 1;}
                for(unsigned i=0;i<n;i++){
                    if(arena[8192+2*i]!=pack(r[i])||arena[8193+2*i]!=pack(im[i])){
                        printf("DIFF n=%u table=%u inv=%u kind=%u i=%u\n",n,table,inverse,kind,i);return 2;}
                    values+=2;
                }cases++;
            }
        }
    }
    if(m16_fft64_banked(arena,22000,0,0,16384,512,512,0)!=-1)return 3;
    if(m16_fft64_banked(arena,22000,0,8192,16384,1024,4096,0)!=-1)return 4;
    if(m16_fft64_banked(arena,22000,0,8192,16384,4096,512,0)!=-1)return 5;
    arena[0]=0x7ff8000000000000ull;
    if(m16_fft64_banked(arena,22000,0,8192,16384,512,512,0)!=-2)return 6;
    // 穷尽所有支持长度、每级地址：bank不同、行映射单射、同级地址不重叠。
    for(unsigned n=256;n<=4096;n*=2){if(n==1024)continue;
        for(unsigned half=1;half<n;half*=2){unsigned char seen[4096]={0};
            for(unsigned i=0;i<n/2;i++){unsigned k=i&(half-1),a=2*i-k,b=a+half;
                unsigned x=a^b,par=0;for(;x;x>>=1)par^=x&1;
                if(par!=1||seen[a]++||seen[b]++)return 7;
            }
        }
    }
    printf("FFT64_BANKED_SCALAR_BIT_EXACT values=%llu cases=%u address_proof=PASS\n",values,cases);
    return 0;
}

}
int old_modes_check(){
    for(unsigned i=0;i<PORT_DEPTH;i++)arena[i]=GUARD;
    if(!add(0,1,4,9,5,false)||!add(1,3,4,34,3,false)||!add(2,3,3,34,16,false)||!add(3,3,1,1,1,true))return 1;
    m16_projection_job plan[8];
    if(m16_projection_validate(arena,VALID_WORDS,0,4,plan))return 2;
    if(m16_shared_projection_engine(arena,VALID_WORDS,0,4)!=4)return 3;
    for(unsigned j=0;j<4;j++){
        const m16_projection_job &p=plan[j];
        if(arena[p.report]!=p.tag || arena[p.report+1]!=((uint64_t)p.nr<<32))return 4;
        uint64_t expected=j==0?fp(5.5f,4.f):j==1?dp(374.):j==2?dp(374.):((uint64_t)(1023-293)<<52);
        for(unsigned i=0;i<p.nr;i++)if(arena[p.out+i]!=expected){
            printf("三模式结果不一致 job=%u result=%u actual=%llx expected=%llx\n",j,i,
                (unsigned long long)arena[p.out+i],(unsigned long long)expected);return 5;
        }
        if(arena[p.out+p.nr]!=GUARD || arena[p.obs+p.no]!=GUARD || arena[p.coef+p.nc]!=GUARD)return 6;
    }
    for(unsigned i=cursor;i<PORT_DEPTH;i++)if(arena[i]!=GUARD)return 7;
    /* 满20帧、34频点、16候选：验证每源累加和边界，不使用单频缩小版冒充。 */
    for(unsigned i=0;i<PORT_DEPTH;i++)arena[i]=GUARD;
    cursor=64;if(!add(0,3,20,34,16,false))return 8;
    if(m16_projection_validate(arena,VALID_WORDS,0,1,plan))return 9;
    if(m16_shared_projection_engine(arena,VALID_WORDS,0,1)!=1)return 10;
    for(unsigned i=0;i<320;i++)if(arena[plan[0].out+i]!=dp(374.))return 11;
    if(arena[plan[0].out+320]!=GUARD)return 12;

    /* 使用独立标量FP64原顺序，而非常量值检查双通道和分块后的数值。 */
    unsigned compared=0,seed=20261027;
    const unsigned dimensions[4][3]={{20,34,16},{20,8,5},{3,34,1},{1,1,1}};
    for(unsigned shape=0;shape<4;shape++)for(unsigned tiny=0;tiny<2;tiny++){
        for(unsigned i=0;i<PORT_DEPTH;i++)arena[i]=GUARD;
        cursor=64;unsigned f=dimensions[shape][0],bins=dimensions[shape][1],c=dimensions[shape][2];
        if(!add(0,3,f,bins,c,false)||m16_projection_validate(arena,VALID_WORDS,0,1,plan))return 20;
        const m16_projection_job &j=plan[0];
        for(unsigned kind=0;kind<2;kind++){
            unsigned base=kind?j.coef:j.obs,words=kind?j.nc:j.no;
            for(unsigned i=0;i<words;i++){
                union{uint32_t u;float f;}a,b;
                seed=1664525u*seed+1013904223u;a.u=tiny?(seed&0x800007ffu):((seed&0x80000000u)|0x3e000000u|(seed&0x007fffffu));
                seed=1664525u*seed+1013904223u;b.u=tiny?(seed&0x800007ffu):((seed&0x80000000u)|0x3e000000u|(seed&0x007fffffu));
                arena[base+i]=fp(a.f,b.f);
            }
        }
        double expected[320];
        for(unsigned frame=0;frame<f;frame++)for(unsigned candidate=0;candidate<c;candidate++){
            double acc=0.;
            for(unsigned pair=0;pair<32;pair++)for(unsigned b=0;b<bins;b++){
                uint64_t x=arena[j.obs+(frame*32+pair)*bins+b],y=arena[j.coef+(pair*bins+b)*c+candidate];
                union{uint32_t u;float f;}xr,xi,yr,yi;xr.u=(uint32_t)x;xi.u=(uint32_t)(x>>32);yr.u=(uint32_t)y;yi.u=(uint32_t)(y>>32);
                acc+=(double)xr.f*(double)yr.f+(double)xi.f*(double)yi.f;
            }
            expected[frame*c+candidate]=acc;
        }
        if(m16_shared_projection_engine(arena,VALID_WORDS,0,1)!=1)return 21;
        for(unsigned i=0;i<f*c;i++){if(arena[j.out+i]!=dp(expected[i]))return 22;compared++;}
        if(arena[j.out+j.nr]!=GUARD)return 23;
    }
    printf("FRAME_REUSE_RANDOM_SUBNORMAL_SCALAR_EXACT outputs=%u cases=8\n",compared);

    {int rc=fft_numeric::check();if(rc)return 30+rc;}
    puts("M16_FRAME_REUSE_20X34X16_AND_OLD_MODES_BIT_EXACT_PASS");
    return 0;
}
/* APCC前端按同一实际C验证器编译，ARM生产版本仍使用C11单独编译。 */
#include "../protocol/mic16_projection_validate.c"
