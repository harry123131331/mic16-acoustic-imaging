/* 独立标量FP64参考与原有交换/蝶形顺序；不使用被测bank函数。 */
#include "fft64_banked.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
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
int main(){unsigned long long values=0;unsigned cases=0;
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
