
/* 固定数值输入的厂商FFT替换检查；不访问板卡，不训练，不改变门限。 */
#include "mic16_response_recurrence.h"
#include "mic16_statistics_neon.h"
#include "mic16_beam_neon.h"
#include "mic16_scanner_neon.h"
#include "mic16_simd_fft.h"
#include "mic16_dataflow_fast.h"
#include "mic16_resample_pipeline.h"
#include "mic16_beam_tiles.h"
#include "mic16_signature_dictionary.h"
#include "mic16_pipeline_unit.h"
#include <stdint.h>
#include <stddef.h>
extern int printf(const char *,...),atoi(const char *),fclose(void *);
extern void *fopen(const char *,const char *),*memcpy(void *,const void *,size_t);
extern size_t fread(void *,size_t,size_t,void *),fwrite(const void *,size_t,size_t,void *);
extern int fgetc(void *);
extern void m16_shared_previous_fft(double *,double *,const double *,const double *,unsigned,unsigned);
static unsigned phase,which,records,bypasses,io_errors,profile_errors;
static void *input_file,*hash_file;
static float block[2048],bins[807*2];
static uint64_t clock_counter;
static uint64_t tick(void *p){(void)p;return ++clock_counter;}
static unsigned hash32(const float *a,unsigned n){
 unsigned h=2166136261u,i;union{float f;unsigned u;}v;
 for(i=0;i<n;i++){v.f=a[i];h=(h^v.u)*16777619u;}return h;
}
void m16_real_fft_transform(double *r,double *im,const double *wr,const double *wi,unsigned n,unsigned cooperate){
 unsigned i,h=0;double maximum=0.;
 if(n!=4096 || !m16_simd_fft_enabled || !phase){m16_shared_previous_fft(r,im,wr,wi,n,cooperate);return;}
 for(i=0;i<n;i++){double a=r[i];if(a<0.)a=-a;if(a>maximum)maximum=a;}
 /* 保留现有混合FFT的动态范围回退，不能把下溢行为混作加速。 */
 if(maximum!=0. && (maximum<1e-15 || maximum>2.)){
  bypasses++;m16_shared_previous_fft(r,im,wr,wi,n,cooperate);return;
 }
 for(i=0;i<2048;i++)block[i]=(float)r[i];
 for(i=0;i<4096;i++)if(im[i]!=0. || (i>=2048 && r[i]!=0.))io_errors++;
 h=hash32(block,2048);records++;
 if(phase==1){
  if(fwrite(block,sizeof(float),2048,input_file)!=2048 || fwrite(&h,sizeof(h),1,hash_file)!=1)io_errors++;
  m16_shared_previous_fft(r,im,wr,wi,n,cooperate);
 }else{
  unsigned expected=0;
  if(fread(bins,sizeof(float),1614,input_file)!=1614 || fread(&expected,sizeof(expected),1,hash_file)!=1 || h!=expected)io_errors++;
  /* 本调用者只使用正频点32至838；其它频点清零以暴露未声明的消费。 */
  for(i=0;i<n;i++)r[i]=im[i]=0.;
  for(i=0;i<807;i++){r[i+32]=bins[2*i];im[i+32]=bins[2*i+1];}
 }
}
static void configure(void){
 /* 数值基线开关与7.336秒保留ARM路径绑定，候选阶段才开启混合FFT/FIR。 */
 m16_fft_fast_select(phase?3:0);m16_feature_fast_select(3);m16_dataflow_select(phase?2:0);
 m16_real_fft_select(phase?3:0);m16_simd_fft_select(phase?1:0);m16_resample_pipeline_select(phase?1:0);
 m16_beam_tiles_select(0);m16_signature_dictionary_select(0);m16_statistics_neon_select(0);
 m16_beam_neon_select(0);m16_scanner_neon_select(0);m16_response_recurrence_select(0);
 m16_dataflow_clear_signatures();
}
static unsigned checksum_pcm,checksum_reference;
static int save_observation(const char *path,const mic16_pipeline_observation *o,const mic16_pipeline_workspace *w){
 unsigned header[12]={0x4d313653,phase,which,checksum_pcm,checksum_reference,o->candidate_count,
 records,bypasses,io_errors,profile_errors,0,0};void *f=fopen(path,"wb");if(!f)return 1;
 io_errors+=fwrite(header,4,12,f)!=12;
 io_errors+=fwrite(w->best_indices,2,480,f)!=480;
 io_errors+=fwrite(o->features,4,41600,f)!=41600;
 io_errors+=fwrite(o->logits,4,16,f)!=16;
 io_errors+=fwrite(o->directions,8,32,f)!=32;
 io_errors+=fwrite(o->scores,8,16,f)!=16;
 io_errors+=fwrite(o->valid,1,16,f)!=16;
 io_errors+=fclose(f)!=0;return io_errors;
}
static mic16_pipeline_observation *last_o;static mic16_pipeline_workspace *last_w;
int measured_pipeline_observe(const float *x,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 mic16_profile_clock c={0,tick};mic16_pipeline_profile p;int rc;
 checksum_pcm=hash32(x,40960*16);checksum_reference=hash32(q,8192*16);
 m16_dataflow_begin();rc=mic16_pipeline_observe_batched_timed(x,q,o,w,&c,&p);m16_dataflow_end();
 profile_errors+=p.reads!=12 || !p.clock_ok || p.completed_mask!=63 || p.attempted_mask!=63;
 io_errors+=checksum_pcm!=hash32(x,40960*16) || checksum_reference!=hash32(q,8192*16);
 last_o=o;last_w=w;return rc;
}
static int begin_files(const char *bin,const char *hash){
 if(!phase)return 0;
 input_file=fopen(bin,phase==1?"wb":"rb");hash_file=fopen(hash,phase==1?"wb":"rb");
 return !input_file || !hash_file;
}
static void end_files(void){
 if(!phase)return;
 if(phase==2 && (fgetc(input_file)!=-1 || fgetc(hash_file)!=-1))io_errors++;
 io_errors+=fclose(input_file)!=0;io_errors+=fclose(hash_file)!=0;
}

extern double floor(double),sin(double);
static float x[40960*16],q[8192*16],wave[3][41152];
static mic16_pipeline_workspace w;static mic16_pipeline_observation o;
static unsigned next(unsigned *s){*s=*s*1664525u+1013904223u;return *s;}
static float noise(unsigned *s){return ((int)(next(s)>>16)-32768)*0x1p-15f;}
static unsigned hash(const float *a,unsigned n){unsigned i,h=2166136261u;union{float f;unsigned u;}v;for(i=0;i<n;i++){v.f=a[i];h=(h^v.u)*16777619u;}return h;}
static double difference(double a,double b){double d=a-b;return d<0.?-d:d;}
static void prepare(unsigned scenario){
 unsigned i,c,s,state=202610220u+scenario,quiet_state=412001u+scenario,ns=scenario==0?1:scenario==2?3:scenario==4?0:2;
 double dirs[3][2]={{-35.,5.},{30.,0.},{40.,10.}},gains[3]={.08,.05,.035};float arrival[3][16],lags[32];
 if(scenario==0){dirs[0][0]=-25.;dirs[0][1]=10.;}
 if(scenario==2){dirs[0][0]=-40.;dirs[0][1]=10.;dirs[1][0]=0.;dirs[1][1]=-5.;}
 if(scenario==3){gains[0]=.1;gains[1]=.001;}
 for(s=0;s<ns;s++){
  mic16_candidate_geometry(dirs[s],arrival[s],lags);
  for(i=0;i<41152;i++)wave[s][i]=scenario==5?(float)sin(2.*3.14159265358979323846*(s?2200.:1000.)*i/39062.5):noise(&state);
 }
 for(i=0;i<8192*16;i++)q[i]=noise(&quiet_state)*1e-4f;
 for(i=0;i<40960;i++)for(c=0;c<16;c++){
  double v=(double)noise(&state)*(scenario==3?1e-4:.001);
  for(s=0;s<ns;s++){double t=(double)i+64.-(double)arrival[s][c];unsigned lo=(unsigned)floor(t);double f=t-lo;
   v+=gains[s]*((double)wave[s][lo]+((double)wave[s][lo+1]-(double)wave[s][lo])*f);}
  x[i*16+c]=(float)v;
 }
}

int main(int argc,char **argv){int rc;
 if(argc!=6)return 2;phase=(unsigned)atoi(argv[1]);which=(unsigned)atoi(argv[2]);
 if(phase>2 || which>5 || begin_files(argv[3],argv[4]))return 3;
 prepare(which);configure();rc=measured_pipeline_observe(x,q,&o,&w);end_files();
 if(last_o)save_observation(argv[5],last_o,last_w);
 printf("SHARED_PLANE phase=%u case=%u candidates=%u records=%u bypass=%u io=%u profile=%u rc=%d\n",
 phase,which,o.candidate_count,records,bypasses,io_errors,profile_errors,rc);
 return rc || io_errors || profile_errors?1:0;
}
