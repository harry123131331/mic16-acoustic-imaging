/* 受控寄存器模型只核验驱动合同，不代替厂商FFT模型或实板性能。 */
#include "mic16_frontend_runtime.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
extern void *memcpy(void *,const void *,size_t);
extern int memcmp(const void *,const void *,size_t);
extern double nearbyint(double);
static m16_front_runtime w __attribute__((aligned(64)));
static mic16_spectral_frame out,reference,saved;
static float input[32768];static uint64_t expected[12912];
static unsigned scenario,checks,errors,starts,invalidates,flushes,barriers,polls,writes,pack_errors;
static uint32_t regs[16];static uint64_t tick;static unsigned active;
void m16_cooperate(void){}
#define CHECK(c) do{checks++;if(!(c)){errors++;printf("FRONT_RUNTIME_ERROR case=%u line=%u\n",scenario,(unsigned)__LINE__);}}while(0)
static uint64_t packed(float a,float b){union {float f;uint32_t u;} x,y;x.f=a;y.f=b;return x.u|((uint64_t)y.u<<32);}
static uint32_t read_reg(void *unused,unsigned off){
 (void)unused;if(off)return regs[off/4];
 if(!active)return scenario==4?0:4;
 polls++;
 if(scenario==5)return 1;
 if(scenario==6 && polls==1)return 2;
 active=0;return 6;
}
static void write_reg(void *unused,unsigned off,uint32_t value){
 unsigned i;(void)unused;writes++;regs[off/4]=value;
 if(off || value!=1)return;
 starts++;active=1;polls=0;
 if(regs[0x10/4]!=0x04000000u || regs[0x14/4] || regs[0x1c/4] || regs[0x24/4]!=16384 || regs[0x2c/4]!=2048)pack_errors++;
 for(i=0;i<32768;i++){
  uint32_t actual=(uint32_t)(w.words[i/2]>>(i%2?32:0));
  int32_t q=(int32_t)nearbyint((double)input[i]*2147483648.);
  if(actual!=(uint32_t)q)pack_errors++;
 }
 memcpy(w.words+16384,expected,sizeof(expected));
 if(scenario==7)w.after[2]=0;
 if(scenario==8)w.words[16384]=UINT64_C(0x000000007f800000);
}
static void flush(void *unused,const void *p,unsigned n){(void)unused;(void)p;(void)n;flushes++;}
static void invalidate(void *unused,const void *p,unsigned n){(void)unused;(void)p;(void)n;invalidates++;}
static uint64_t ticks(void *unused){(void)unused;tick++;return scenario==9 && tick==3?0:tick;}
static void barrier(void *unused){(void)unused;barriers++;}
static m16_scanner_io io={0,read_reg,write_reg,flush,invalidate,ticks,barrier,0x04000000u,20};
static void reset(void){
 memset(&w,0,sizeof(w));memset(&out,0x5a,sizeof(out));memcpy(&saved,&out,sizeof(out));
 starts=invalidates=flushes=barriers=polls=writes=pack_errors=active=0;tick=0;
 CHECK(m16_front_runtime_init(&w,&io)==0);
}
int main(void){
 unsigned i,c,b,state=20261022u;int rc;m16_front_report r;
 for(i=0;i<32768;i++){state=state*1664525u+1013904223u;input[i]=((int)(state>>16)-32768)*0x1p-16f;}
 input[0]=-1.f;input[1]=0x1p-32f;input[2]=3*0x1p-32f;input[3]=-3*0x1p-32f;input[4]=-0.f;
 for(c=0;c<16;c++)for(b=0;b<807;b++){
  float a=(float)((int)(b%31)-15)*65536.f,d=(float)((int)(c%7)-3)*32768.f;
  expected[c*807+b]=packed(a,d);
  reference.spectrum[(b*16+c)*2]=(double)a/2147483648.;
  reference.spectrum[(b*16+c)*2+1]=(double)d/2147483648.;
 }
 CHECK(mic16_spectral_observe(&reference)==0);
 scenario=0;reset();rc=m16_front_runtime_frame(&w,input,&out,&r);
 CHECK(rc==0);CHECK(memcmp(&out,&reference,sizeof(out))==0);CHECK(starts==1 && pack_errors==0);
 CHECK(flushes==3 && invalidates==3 && barriers>=4);CHECK(r.completed_frames==1 && r.input_scale==31 && !r.quarantined);
 CHECK(r.total_ticks==r.packing_ticks+r.device_ticks+r.decoding_ticks+1);
 /* 先完整核验再启动，末尾NaN和+1均不能留下部分新包。 */
 scenario=1;reset();{union {uint32_t u;float f;} nan;float old=input[32767];nan.u=0x7fc00001;input[32767]=nan.f;
  w.words[0]=123;CHECK(m16_front_runtime_frame(&w,input,&out,&r)==M16_FRONT_INVALID);
  CHECK(!writes && !starts && w.words[0]==123 && memcmp(&out,&saved,sizeof(out))==0);input[32767]=old;}
 scenario=2;reset();{float old=input[0];input[0]=1.f;w.words[0]=123;
  CHECK(m16_front_runtime_frame(&w,input,&out,&r)==M16_FRONT_RANGE);CHECK(!writes && w.words[0]==123 && memcmp(&out,&saved,sizeof(out))==0);input[0]=old;}
 scenario=3;reset();CHECK(m16_front_runtime_frame(&w,input,(mic16_spectral_frame *)(void *)input,&r)==M16_FRONT_INVALID);
 CHECK(m16_front_runtime_frame(&w,input,&out,(m16_front_report *)(void *)&w)==M16_FRONT_INVALID);CHECK(!starts);
 scenario=4;reset();w.words[0]=123;CHECK(m16_front_runtime_frame(&w,input,&out,&r)==M16_FRONT_BUSY);CHECK(!writes && w.words[0]==123);
 scenario=5;reset();CHECK(m16_front_runtime_frame(&w,input,&out,&r)==M16_FRONT_TIMEOUT);CHECK(w.quarantined && r.quarantined && !r.completed_frames);
 CHECK(memcmp(&out,&saved,sizeof(out))==0);i=writes;w.words[0]=123;
 CHECK(m16_front_runtime_frame(&w,input,&out,&r)==M16_FRONT_INVALID && writes==i && w.words[0]==123);CHECK(m16_front_runtime_init(&w,&io)==M16_FRONT_INVALID);
 scenario=6;reset();CHECK(m16_front_runtime_frame(&w,input,&out,&r)==0);CHECK(polls==2 && r.completed_frames==1 && memcmp(&out,&reference,sizeof(out))==0);
 scenario=7;reset();CHECK(m16_front_runtime_frame(&w,input,&out,&r)==M16_FRONT_GUARD);CHECK(w.quarantined && memcmp(&out,&saved,sizeof(out))==0);
 scenario=8;reset();CHECK(m16_front_runtime_frame(&w,input,&out,&r)==M16_FRONT_GUARD);CHECK(w.quarantined && memcmp(&out,&saved,sizeof(out))==0);
 scenario=9;reset();CHECK(m16_front_runtime_frame(&w,input,&out,&r)==M16_FRONT_CLOCK);CHECK(w.quarantined && memcmp(&out,&saved,sizeof(out))==0);
 scenario=10;reset();io.arena_address=0x10000000u;CHECK(m16_front_runtime_init(&w,&io)==M16_FRONT_INVALID);io.arena_address=0x04000000u;
 printf("FRONT_RUNTIME_%s checks=%u cases=11 errors=%u NOT_VENDOR_FFT_OR_BOARD_TIMING\n",errors?"FAIL":"PASS",checks,errors);return errors?1:0;
}
