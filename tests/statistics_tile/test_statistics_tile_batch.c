#include "mic16_statistics_tile_runtime.h"
#include "mic16_pipeline_unit.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
extern int m16_reference_statistics_neon(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
extern int m16_tile_host_bind(void);
static mic16_extended_workspace w;
static float reference[16*20*18],current[16*20*18];
static double dirs[32];
int measured_pipeline_observe(const float *a,const float *b,mic16_pipeline_observation *c,mic16_pipeline_workspace *d){(void)a;(void)b;(void)c;(void)d;return -1;}
int application_probe_observe(const float *a,const float *b,mic16_pipeline_observation *c,mic16_pipeline_workspace *d){return measured_pipeline_observe(a,b,c,d);}
int m16_application_test_environment(unsigned *f,unsigned *c){*f=0;*c=15u<<20;return 1;}
static unsigned state=20261024u,checks,errors;
static unsigned next(void){state=state*1664525u+1013904223u;return state;}
static void check(int ok,const char *label){checks++;if(!ok){errors++;printf("BATCH_FAIL %s\n",label);}}
static int available(void *c){(void)c;return 1;}
static int failing(void *c,unsigned f,unsigned b,unsigned n,unsigned dirty){(void)c;(void)f;(void)b;(void)n;(void)dirty;return -987;}
int main(void){unsigned pattern,n,i;float sentinel;
 for(pattern=0;pattern<4;pattern++){
  for(i=0;i<67*257*16*2;i++)w.observed[i]=pattern==0?0.:((int)(next()>>16)-32768)*(pattern==1?0x1p-40:pattern==2?0x1p-28:0x1p-15);
  for(i=0;i<16*67*64;i++)w.beam_power[i]=pattern==0?0.:((next()>>16)+1)*0x1p-20;
  for(i=0;i<67*64;i++)w.channel_power[i]=pattern==0?0.:((next()>>16)+1)*0x1p-20;
  for(i=0;i<64;i++)w.quiet_power[i]=pattern==0?0.:((next()>>16)+1)*0x1p-24;
  for(i=0;i<16;i++){dirs[i*2]=-55.+i*7.;dirs[i*2+1]=-20.+i*2.;}
  for(n=2;n<=16;n++){
   mic16_feature_reuse_report a={0},b={0};unsigned groups=(n+3)/4;
   check(!m16_reference_statistics_neon(dirs,n,reference,&w,&a),"原FP32实现");
   check(!m16_tile_host_bind(),"真实驱动绑定");
   for(i=0;i<16*20*18;i++)current[i]=12345.f;
   check(!m16_tile_batch_compute(dirs,n,current,&w,&b),"真实分块执行");
   check(!memcmp(reference,current,n*20*18*sizeof(float)),"全部统计特征逐字节一致");
   check(a.pair_normalizations==b.pair_normalizations && b.pair_normalizations==67*256*32,"归一化次数保持");
   check(m16_tile_batch_counts.rows==67*n && m16_tile_batch_counts.batches==17*32*groups &&
         m16_tile_batch_counts.rotation_groups==67*256*groups,"分块和工作量保持");
   if(n<16)check(current[n*20*18]==12345.f,"不越写尾候选");
   printf("BATCH_TAIL pattern=%u count=%u batches=%u byte_exact=1\n",pattern,n,m16_tile_batch_counts.batches);
  }
 }
 check(m16_tile_batch_compute(dirs,1,current,&w,0)==-1,"拒绝单候选外部误用");
 check(m16_tile_batch_compute(0,2,current,&w,0)==-1,"拒绝空方向");
 {static uint64_t arena[2304] __attribute__((aligned(64)));m16_tile_batch_backend f={0,arena,available,failing};
  check(!m16_tile_batch_bind(&f),"故障后端绑定");sentinel=current[0]=54321.f;
  check(m16_tile_batch_compute(dirs,2,current,&w,0)==-987,"保留硬件故障码不回退");
  check(current[0]==sentinel,"故障不发布半份特征");}
 printf("TILE_BATCH_TAIL_PASS checks=%u errors=%u patterns=4 counts=2..16 BYTE_EXACT_FP32_NOT_BOARD\n",checks,errors);
 return errors?1:0;
}
