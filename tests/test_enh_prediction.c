/* 只验冻结参数接口，不把指定候选和数值输入作为真实检出率。 */
#include "mic16_enh_prediction.h"
#include <stddef.h>
extern int printf(const char *,...);
extern int memcmp(const void *,const void *,size_t);
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
static float pcm[40960*16],quiet[8192*16];
static double dirs[32];static unsigned char valid[16];
static mic16_cycle_output tracks;
static mic16_extended_workspace shared;
static m16_enh_workspace work;
static float masks[8][67*64],features[8][67*64*10],weights[320];
static unsigned ids[8],candidates[8],checks,errors,calls,compare,fail_after;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("PREDICTION_ERROR line=%u\n",(unsigned)__LINE__);}}while(0)
static int accept(void *unused,unsigned id,unsigned c,const float *mask){
 unsigned n=calls++;(void)unused;CHECK(n<8);if(n>=8)return -1;
 if(compare){CHECK(ids[n]==id && candidates[n]==c);CHECK(!memcmp(masks[n],mask,sizeof(masks[n])));
  CHECK(!memcmp(features[n],shared.spectral10,sizeof(features[n])));}
 else{ids[n]=id;candidates[n]=c;memcpy(masks[n],mask,sizeof(masks[n]));memcpy(features[n],shared.spectral10,sizeof(features[n]));}
 return fail_after && calls==fail_after;
}
static int legacy(void *p,unsigned id,unsigned c,const float *x,const float *mask,
 const double *spectrum,const double *wdas,const double *enhanced,unsigned n){
 CHECK(x==shared.spectral10 && spectrum==work.spectrum && wdas==work.wdas && enhanced==work.enhanced && n==16778);
 return accept(p,id,c,mask);
}
static void prepare(unsigned n){
 unsigned i;memset(&tracks,0,sizeof(tracks));tracks.count=(int)n;tracks.confirmed_count=(int)n;
 for(i=0;i<16;i++){dirs[2*i]=-75.+i*10.;dirs[2*i+1]=(i%3)*5.-5.;valid[i]=1;}
 for(i=0;i<n;i++){mic16_track *t=tracks.tracks+i;t->source_id=100+i;t->azimuth=dirs[2*i];
  t->elevation=dirs[2*i+1];t->score=.9;t->confirmed_this_cycle=1;}
}
static void both(int expected){
 int r;calls=compare=0;r=m16_enh_window(pcm,quiet,dirs,valid,&tracks,&shared,&work,legacy,0);CHECK(r==expected && !calls);
 calls=0;r=m16_enh_predict_window(pcm,quiet,dirs,valid,&tracks,&shared,&work,accept,0);CHECK(r==expected && !calls);
}
int main(void){
 unsigned i;unsigned state=20261021;int r;
 for(i=0;i<40960*16;i++){state=state*1664525u+1013904223u;pcm[i]=(float)((int)(state>>16)-32768)/262144.f;}
 for(i=0;i<8192*16;i++){state=state*1664525u+1013904223u;quiet[i]=(float)((int)(state>>16)-32768)/4194304.f;}
 prepare(0);both(0);prepare(1);tracks.tracks[0].confirmed_this_cycle=0;tracks.confirmed_count=0;both(0);
 prepare(1);tracks.count=9;both(-1);prepare(1);tracks.confirmed_count=2;both(-1);
 prepare(1);tracks.tracks[0].score=.64;both(-1);prepare(1);tracks.tracks[0].source_id=0;both(-1);
 prepare(1);valid[0]=2;both(-1);prepare(1);dirs[0]=-90.;both(-1);
 prepare(1);pcm[0]=2.f;both(-1);pcm[0]=0.f;quiet[0]=2.f;both(-1);quiet[0]=0.f;
 prepare(1);{unsigned bits=0x7fc00000u;memcpy(pcm,&bits,4);}both(-1);pcm[0]=0.f;
 prepare(2);tracks.tracks[1].source_id=tracks.tracks[0].source_id;both(-1);
 prepare(2);tracks.tracks[1].azimuth=dirs[0];tracks.tracks[1].elevation=dirs[1];both(-1);
 prepare(1);calls=0;
 CHECK(m16_enh_window(0,quiet,dirs,valid,&tracks,&shared,&work,legacy,0)==-1);
 CHECK(m16_enh_predict_window(0,quiet,dirs,valid,&tracks,&shared,&work,accept,0)==-1);
 CHECK(m16_enh_window(pcm,pcm,dirs,valid,&tracks,&shared,&work,legacy,0)==-1);
 CHECK(m16_enh_predict_window(pcm,pcm,dirs,valid,&tracks,&shared,&work,accept,0)==-1);
 CHECK(m16_enh_window(pcm,quiet,dirs,valid,&tracks,&shared,&work,0,0)==-1);
 CHECK(m16_enh_predict_window(pcm,quiet,dirs,valid,&tracks,&shared,&work,0,0)==-1 && !calls);
 /* 当前8源、全部16候选竞争；随后在第二源回调注入失败，比较相同有效前缀。 */
 for(i=0;i<2;i++){
  prepare(i?2:8);fail_after=i?2:0;calls=compare=0;memset(&shared,0,sizeof(shared));memset(&work,0,sizeof(work));
  r=m16_enh_window(pcm,quiet,dirs,valid,&tracks,&shared,&work,legacy,0);CHECK(r==(i?-6:8) && calls==(i?2u:8u));
  memcpy(weights,work.weights,sizeof(weights));calls=0;compare=1;memset(&shared,0,sizeof(shared));memset(&work,0,sizeof(work));
  r=m16_enh_predict_window(pcm,quiet,dirs,valid,&tracks,&shared,&work,accept,0);CHECK(r==(i?-6:8) && calls==(i?2u:8u));
  CHECK(!memcmp(weights,work.weights,sizeof(weights)));
 }
 printf("PREDICTION_%s checks=%u errors=%u max_current_sources=8 valid_candidates=16 BOARD_OPERATIONS=0\n",errors?"FAIL":"PASS",checks,errors);
 return errors?1:0;
}
