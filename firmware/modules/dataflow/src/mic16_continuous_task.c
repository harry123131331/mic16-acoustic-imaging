#include "mic16_continuous_task.h"
#include "mic16_physical.h"
#include "mic16_resample_tables.h"
#include <math.h>
#include <string.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d435431u
#define PI 3.14159265358979323846264338327950288
static int valid(const m16_continuous_task *t){return t && !((uintptr_t)t%_Alignof(m16_continuous_task)) && t->magic==MAGIC;}
static int finite64(double x){return x==x && x>=-1.7976931348623157e308 && x<=1.7976931348623157e308;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);}
static int environment(void){
#if defined(__arm__)
 unsigned x;__asm__ volatile("vmrs %0,fpscr":"=r"(x));return !(x&0x01c00000u);
#else
 if(fegetround()!=FE_TONEAREST)return 0;
#if defined(__SSE__)
 if(_mm_getcsr()&0xe040u)return 0;
#endif
 return 1;
#endif
}
static int fail(m16_continuous_task *t,int rc){
 (void)m16_complex_fft_stop(&t->fft);(void)m16_resample_task_stop(&t->resample);
 t->work->failed=1;t->phase=M16_CT_FAILED;t->error=rc;return rc;
}
static const m16_cont_prediction *record(const m16_cont_workspace *w,int64_t index){
 if(index<0 || (uint64_t)index>=w->windows || (uint64_t)index+3u<w->windows)return 0;
 return w->prediction+index%3;
}
static int find(const m16_cont_prediction *p,uint32_t id){unsigned i;if(p)for(i=0;i<p->count;i++)if(p->ids[i]==id)return (int)i;return -1;}
static double sample(const m16_cont_workspace *w,int64_t n,unsigned c){return n<0?0.:(double)w->pcm[((uint64_t)n/40960u)%3u][((uint64_t)n%40960u)*16u+c];}
static int geometry(const double *d,m16_cont_steering *s){
 float arrivals[16],lags[32];int c,k,m;
 if(mic16_candidate_geometry(d,arrivals,lags))return -1;
 for(c=0;c<16;c++){
  double v=10.-arrivals[c],fraction;s->delay[c]=(int)floor(v);fraction=v-s->delay[c];
  if(s->delay[c]<0 || s->delay[c]+5>=32)return -1;
  for(k=0;k<6;k++){double a=1.;for(m=0;m<6;m++)if(m!=k)a*=(fraction-m)/(double)(k-m);s->coefficient[c][k]=(float)a;}
 }
 return 0;
}
static double fade(unsigned n){return .5-.5*cos(PI*(double)n/624.);}
static double interpolated(const float *mask,double position,unsigned band){
 unsigned a,b;double f;if(position<0.)position=0.;if(position>66.)position=66.;
 a=(unsigned)floor(position);b=a<66u?a+1u:66u;f=position-a;
 return (1.-f)*(double)mask[a*64u+band]+f*(double)mask[b*64u+band];
}
int m16_continuous_task_init(m16_continuous_task *t,m16_cont_workspace *w,uint32_t epoch){
 unsigned i;
 if(!t || (uintptr_t)t%_Alignof(m16_continuous_task) || t->magic || !w || (uintptr_t)w%_Alignof(m16_cont_workspace) || !epoch || !apart(t,sizeof(*t),w,sizeof(*w)) || !environment())return M16_CT_INVALID;
 *t=(m16_continuous_task){0};w->epoch=epoch;w->windows=w->emitted=w->closed=w->failed=0;
 for(i=0;i<256u;i++){w->twiddle_re[i]=cos(2.*PI*i/512.);w->twiddle_im[i]=-sin(2.*PI*i/512.);}
 if(m16_complex_fft_init(&t->fft,w->fft_re,w->fft_im,512,w->twiddle_re,w->twiddle_im,512) || m16_resample_task_init(&t->resample))return M16_CT_INVALID;
 t->magic=MAGIC;t->work=w;return 0;
}
int m16_continuous_task_releasable(const m16_continuous_task *t){return valid(t) &&
 (t->phase==M16_CT_IDLE || t->phase==M16_CT_COMPLETE || t->phase==M16_CT_STOPPED || t->phase==M16_CT_FAILED) &&
 m16_complex_fft_releasable(&t->fft) && m16_resample_task_releasable(&t->resample);}
int m16_continuous_task_reset(m16_continuous_task *t,uint32_t epoch){
 if(!valid(t) || !epoch || epoch==t->work->epoch)return M16_CT_INVALID;
 if(!m16_continuous_task_releasable(t))return M16_CT_BUSY;
 if(!environment())return M16_CT_FLOAT_ENV;
 t->work->epoch=epoch;t->work->windows=t->work->emitted=t->work->closed=t->work->failed=0;
 t->input=0;t->input_prediction=0;t->phase=M16_CT_IDLE;t->error=t->result_count=0;return 0;
}
int m16_continuous_task_push(m16_continuous_task *t,const float *pcm,const m16_cont_prediction *p,uint64_t first){
 unsigned i,j;m16_cont_steering steering;m16_cont_workspace *w;
 if(!valid(t))return M16_CT_INVALID;
 if(!m16_continuous_task_releasable(t))return M16_CT_BUSY;
 w=t->work;
 if(w->failed || w->closed || !w->epoch)return M16_CT_INVALID;
 if(!pcm || !p || (uintptr_t)pcm%4u || (uintptr_t)p%_Alignof(m16_cont_prediction) || w->windows==0xffffffffu || first!=(uint64_t)w->windows*40960u ||
  !apart(w,sizeof(*w),pcm,40960u*16u*4u) || !apart(w,sizeof(*w),p,sizeof(*p)) || !apart(t,sizeof(*t),pcm,40960u*16u*4u) ||
  !apart(t,sizeof(*t),p,sizeof(*p)) || !apart(p,sizeof(*p),pcm,40960u*16u*4u))return fail(t,M16_CT_INPUT);
 if(!environment())return fail(t,M16_CT_FLOAT_ENV);
 if(p->count>8u)return fail(t,M16_CT_INPUT);
 for(i=0;i<p->count;i++){
  if(!p->ids[i])return fail(t,M16_CT_INPUT);
  for(j=0;j<i;j++)if(p->ids[i]==p->ids[j])return fail(t,M16_CT_INPUT);
  for(j=0;j<2u;j++)if(!finite64(p->directions[i][j]) || p->directions[i][j]<=-90. || p->directions[i][j]>=90.)return fail(t,M16_CT_INPUT);
  if(geometry(p->directions[i],&steering))return fail(t,M16_CT_INPUT);
 }
 t->input=pcm;t->input_prediction=p;t->prediction_count=p->count;t->final=0;t->cursor=t->mask_source=t->weight_frame=t->last_items=t->max_items=0;t->steps=0;t->result_count=0;t->error=0;t->phase=M16_CT_VALIDATE_PCM;return 0;
}
int m16_continuous_task_finish(m16_continuous_task *t){
 if(!valid(t))return M16_CT_INVALID;
 if(!m16_continuous_task_releasable(t))return M16_CT_BUSY;
 if(t->work->failed || t->work->closed || !t->work->epoch)return M16_CT_INVALID;
 if(!environment())return fail(t,M16_CT_FLOAT_ENV);
 t->final=1;t->steps=0;t->last_items=t->max_items=0;t->result_count=0;t->error=0;
 if(!t->work->windows){t->work->closed=1;t->phase=M16_CT_COMPLETE;return 0;}
 t->phase=M16_CT_EMIT_SETUP;return 0;
}
static int resample_begin(m16_continuous_task *t){
 int rc=m16_resample_task_begin64(&t->resample,t->work->raw,t->raw_count,t->work->resampled,M16_CONT_RESAMPLED);
 if(rc)return fail(t,rc);
 t->phase=M16_CT_RESAMPLE;return 1;
}
static void raw_next(m16_continuous_task *t){
 t->n++;t->channel=0;t->side=0;t->value[0]=t->value[1]=0.;
 if(t->n==t->block_stop){t->block++;t->phase=M16_CT_RAW_BLOCK;}
}
static int output_ready(m16_continuous_task *t){
 const m16_cont_prediction *current=record(t->work,t->index);
 t->info.source_id=t->ordinal<t->id_count?t->ids[t->ordinal]:0u;t->info.ordinal=t->ordinal;
 t->info.current_confirmed=t->info.source_id && find(current,t->info.source_id)>=0;t->phase=M16_CT_OUTPUT_READY;return 1;
}
static int step(m16_continuous_task *t){
 m16_cont_workspace *w=t->work;unsigned i,end;int rc;
 if(t->phase==M16_CT_COMPLETE)return 0;
 if(t->phase==M16_CT_STOPPED)return 2;
 if(t->phase==M16_CT_FAILED)return t->error;
 if(t->phase==M16_CT_IDLE)return M16_CT_INVALID;
 if(!environment())return fail(t,M16_CT_FLOAT_ENV);
 if(t->phase==M16_CT_OUTPUT_READY)return 1;
 if(t->phase==M16_CT_VALIDATE_PCM){
  end=t->cursor+64u;if(end>40960u*16u)end=40960u*16u;
  for(i=t->cursor;i<end;i++){if(!(t->input[i]>=-1.f && t->input[i]<=1.f))return fail(t,M16_CT_INPUT);t->last_items++;}
  t->cursor=end;if(end==40960u*16u){t->cursor=0;t->phase=M16_CT_VALIDATE_MASK;}return 1;
 }
 if(t->phase==M16_CT_VALIDATE_MASK){
  if(t->input_prediction->count!=t->prediction_count)return fail(t,M16_CT_INPUT);
  if(t->mask_source==t->prediction_count){t->cursor=0;t->phase=M16_CT_VALIDATE_WEIGHTS;return 1;}
  end=t->cursor+64u;if(end>67u*64u)end=67u*64u;
  for(i=t->cursor;i<end;i++){float m=t->input_prediction->masks[t->mask_source][i];if(!(m>=.8f-1e-6f && m<=1.2f+1e-6f))return fail(t,M16_CT_INPUT);t->last_items++;}
  t->cursor=end;if(end==67u*64u){t->mask_source++;t->cursor=0;}return 1;
 }
 if(t->phase==M16_CT_VALIDATE_WEIGHTS){
  if(t->prediction_count){
   double sum=0.;for(i=0;i<16u;i++){float v=t->input_prediction->weights[t->weight_frame*16u+i];if(!(v>=0.f && v<=1.f))return fail(t,M16_CT_INPUT);sum+=v;t->last_items++;}
   if(sum<1.-1e-6 || sum>1.+1e-6)return fail(t,M16_CT_INPUT);
   if(++t->weight_frame<20u)return 1;
  }
  t->cursor=0;t->phase=M16_CT_COPY_PCM;return 1;
 }
 if(t->phase==M16_CT_COPY_PCM){
  end=t->cursor+64u;if(end>40960u*16u)end=40960u*16u;
  for(i=t->cursor;i<end;i++){float x=t->input[i];if(!(x>=-1.f && x<=1.f))return fail(t,M16_CT_INPUT);w->pcm[w->windows%3u][i]=x;t->last_items++;}
  t->cursor=end;if(end==40960u*16u){t->cursor=0;t->phase=M16_CT_COPY_PREDICTION;}return 1;
 }
 if(t->phase==M16_CT_COPY_PREDICTION){
  unsigned bytes=256u;if(bytes>sizeof(m16_cont_prediction)-t->cursor)bytes=(unsigned)sizeof(m16_cont_prediction)-t->cursor;
  memcpy((unsigned char *)&w->prediction[w->windows%3u]+t->cursor,(const unsigned char *)t->input_prediction+t->cursor,bytes);t->cursor+=bytes;t->last_items=bytes;
  if(t->cursor==sizeof(m16_cont_prediction)){
   const m16_cont_prediction *p=&w->prediction[w->windows%3u];unsigned j;m16_cont_steering s;
   if(p->count!=t->prediction_count)return fail(t,M16_CT_INPUT);
   for(i=0;i<p->count;i++){
    if(!p->ids[i] || geometry(p->directions[i],&s))return fail(t,M16_CT_INPUT);
    for(j=0;j<i;j++)if(p->ids[i]==p->ids[j])return fail(t,M16_CT_INPUT);
   }
   w->windows++;
   if(w->windows<2u){t->phase=M16_CT_COMPLETE;return 0;}
   t->phase=M16_CT_EMIT_SETUP;
  }
  return 1;
 }
 if(t->phase==M16_CT_EMIT_SETUP){
  uint64_t start,end_sample,begin,finish,offset;unsigned keep=t->final?2u:3u,j;
  t->index=w->windows-(t->final?1u:2u);start=(uint64_t)t->index*40960u;end_sample=start+40960u;
  if(w->emitted!=t->index)return fail(t,M16_CT_INPUT);
  t->first=w->windows>keep?w->windows-keep:0u;t->origin=start<2048u?0u:((start-2048u)/625u)*625u;
  t->stop=end_sample+2048u<(uint64_t)w->windows*40960u?end_sample+2048u:(uint64_t)w->windows*40960u;
  if(t->origin<(uint64_t)t->first*40960u || (t->origin && t->origin-(uint64_t)t->first*40960u<32u) || t->stop-t->origin>M16_CONT_RAW)return fail(t,M16_CT_INPUT);
  t->raw_count=(unsigned)(t->stop-t->origin);begin=(start*256u+624u)/625u;finish=(end_sample*256u+624u)/625u;offset=(t->origin/625u)*256u;
  t->samples=(unsigned)(finish-begin);t->selection=(unsigned)(begin-offset);t->id_count=0;
  for(i=t->first;i<w->windows;i++){
   const m16_cont_prediction *p=record(w,i);if(!p)return fail(t,M16_CT_INPUT);
   for(j=0;j<p->count;j++){unsigned k;for(k=0;k<t->id_count;k++)if(t->ids[k]==p->ids[j])break;if(k==t->id_count){if(k>=24u)return fail(t,M16_CT_INPUT);t->ids[t->id_count++]=p->ids[j];}}
  }
  for(i=1;i<t->id_count;i++){uint32_t id=t->ids[i];j=i;while(j && t->ids[j-1u]>id){t->ids[j]=t->ids[j-1u];j--;}t->ids[j]=id;}
  t->info=(m16_cont_info){0};t->info.epoch=w->epoch;t->info.window=t->index;t->info.stream_count=t->id_count;t->info.final=t->final;
  t->info.output_start=begin;t->info.output_end=finish;t->info.input_start=start;t->info.input_end=end_sample;t->info.available_input=(uint64_t)w->windows*40960u;
  t->ordinal=0;t->phase=M16_CT_SOURCE;return 1;
 }
 if(t->phase==M16_CT_SOURCE){t->cursor=0;t->phase=t->ordinal==t->id_count?M16_CT_RAW_REFERENCE:M16_CT_RAW_CLEAR;return 1;}
 if(t->phase==M16_CT_RAW_CLEAR){
  end=t->cursor+64u;if(end>t->raw_count)end=t->raw_count;
  for(i=t->cursor;i<end;i++){w->raw[i]=0.;t->last_items++;}
  t->cursor=end;if(end==t->raw_count){t->block=t->origin/40960u;t->phase=M16_CT_RAW_BLOCK;}return 1;
 }
 if(t->phase==M16_CT_RAW_BLOCK){
  uint32_t id=t->ids[t->ordinal];
  if(t->block>(t->stop-1u)/40960u)return resample_begin(t);
  t->now=record(w,(int64_t)t->block);t->old=record(w,(int64_t)t->block-1);t->prediction_index=find(t->now,id);t->old_index=find(t->old,id);
  if(t->prediction_index>=0 && geometry(t->now->directions[t->prediction_index],t->steering))return fail(t,M16_CT_INPUT);
  if(t->old_index>=0 && geometry(t->old->directions[t->old_index],t->steering+1))return fail(t,M16_CT_INPUT);
  t->n=t->origin>t->block*40960u?t->origin:t->block*40960u;t->block_stop=t->stop<(t->block+1u)*40960u?t->stop:(t->block+1u)*40960u;
  t->side=t->channel=0;t->value[0]=t->value[1]=0.;t->phase=M16_CT_RAW_BEAM;return 1;
 }
 if(t->phase==M16_CT_RAW_BEAM){
  unsigned local=(unsigned)(t->n-t->block*40960u),c,k;int need_cur=t->prediction_index>=0,need_old=t->old_index>=0 && local<625u;
  if(!need_cur && !need_old){w->raw[t->n-t->origin]=0.;raw_next(t);return 1;}
  if(!need_cur && t->side==0)t->side=1;
  if(t->side==0 || need_old){
   const m16_cont_steering *s=t->steering+t->side;
   const float *weights=t->side?t->old->weights+19u*16u:t->now->weights+(local/2048u)*16u;
   for(c=t->channel;c<t->channel+8u;c++){
    double value=0.;
    for(k=0;k<6u;k++){
     int64_t n=(int64_t)t->n-s->delay[c]-(int)k;
     if(n>=0 && ((uint64_t)n>=(uint64_t)w->windows*40960u || (uint64_t)n/40960u+3u<w->windows))return fail(t,M16_CT_INPUT);
     value+=(double)s->coefficient[c][k]*sample(w,n,c);t->last_items++;
    }
    t->value[t->side]+=value*(double)weights[c];t->last_items++;
   }
   t->channel+=8u;if(t->channel<16u)return 1;
   t->channel=0;
   if(t->side==0 && need_old){t->side=1;return 1;}
  }
  {
   double value=0.;
   if(need_cur){value=t->value[0];if(t->block && local<625u){double f=fade(local);value=f*value+(t->old_index>=0?(1.-f)*t->value[1]:0.);}}
   else if(need_old)value=(1.-fade(local))*t->value[1];
   if(!finite64(value))return fail(t,M16_CT_OUTPUT);
   w->raw[t->n-t->origin]=value;
  }
  raw_next(t);return 1;
 }
 if(t->phase==M16_CT_RAW_REFERENCE){
  end=t->cursor+64u;if(end>t->raw_count)end=t->raw_count;
  for(i=t->cursor;i<end;i++){w->raw[i]=sample(w,(int64_t)(t->origin+i),0);t->last_items++;}
  t->cursor=end;if(end==t->raw_count)return resample_begin(t);return 1;
 }
 if(t->phase==M16_CT_RESAMPLE){
  rc=m16_resample_task_step(&t->resample);t->last_items=t->resample.last_items;
  if(rc<0)return fail(t,rc);
  if(rc==2)return fail(t,M16_CT_INVALID);
  if(!rc){
   t->length=t->resample.length;if(t->length>M16_CONT_RESAMPLED || t->selection+t->samples>t->length)return fail(t,M16_CT_OUTPUT);
   t->cursor=0;
   if(t->ordinal==t->id_count)t->phase=M16_CT_CROP;
   else{t->frames=(t->length+255u)/256u+1u;t->total=(t->frames-1u)*256u+512u;if(t->total>M16_CONT_OLA)return fail(t,M16_CT_OUTPUT);t->phase=M16_CT_OLA_CLEAR;}
  }
  return 1;
 }
 if(t->phase==M16_CT_OLA_CLEAR){
  end=t->cursor+32u;if(end>t->total)end=t->total;
  for(i=t->cursor;i<end;i++){w->signal[i]=w->norm[i]=0.;t->last_items+=2u;}
  t->cursor=end;if(end==t->total){t->cursor=t->frame=0;t->phase=M16_CT_MASKS;}return 1;
 }
 if(t->phase==M16_CT_MASKS){
  uint64_t center=(t->origin/625u+t->frame)*625u,window=center/40960u;unsigned local;
  const m16_cont_prediction *now,*old;int ci,oi;
  if(t->final && window>=w->windows)window=w->windows-1u;
  local=(unsigned)(center-window*40960u);now=record(w,(int64_t)window);old=record(w,(int64_t)window-1);
  ci=find(now,t->ids[t->ordinal]);oi=find(old,t->ids[t->ordinal]);end=t->cursor+8u;
  for(i=t->cursor;i<end;i++){
   double m=1.;
   if(ci>=0){m=interpolated(now->masks[ci],(double)local/625.,i);if(oi>=0 && local<625u){double f=fade(local);m=(1.-f)*interpolated(old->masks[oi],(40960.+local)/625.,i)+f*m;}}
   else if(oi>=0 && local<625u)m=interpolated(old->masks[oi],(40960.+local)/625.,i);
   if(!finite64(m))return fail(t,M16_CT_INPUT);
   t->masks[i]=m;t->last_items++;
  }
  t->cursor=end;if(end==64u){t->cursor=0;t->phase=M16_CT_FRAME;}return 1;
 }
 if(t->phase==M16_CT_FRAME){
  end=t->cursor+64u;if(end>512u)end=512u;
  for(i=t->cursor;i<end;i++){int pos=(int)(t->frame*256u+i)-256;w->fft_re[i]=(pos>=0 && pos<(int)t->length?w->resampled[pos]:0.)*m16_periodic_hann512[i];w->fft_im[i]=0.;t->last_items++;}
  t->cursor=end;
  if(end==512u){rc=m16_complex_fft_begin(&t->fft,512,0);if(rc)return fail(t,rc);t->phase=M16_CT_FFT;}return 1;
 }
 if(t->phase==M16_CT_FFT || t->phase==M16_CT_IFFT){
  rc=m16_complex_fft_step(&t->fft);t->last_items=t->fft.last_items;
  if(rc<0)return fail(t,rc);
  if(rc==2)return fail(t,M16_CT_INVALID);
  if(!rc){t->cursor=0;t->phase=t->phase==M16_CT_FFT?M16_CT_MULTIPLY:M16_CT_OLA;}return 1;
 }
 if(t->phase==M16_CT_MULTIPLY){
  end=t->cursor+32u;if(end>257u)end=257u;
  for(i=t->cursor;i<end;i++){double m=t->masks[i?(i-1u)/4u:0];w->fft_re[i]*=m;w->fft_im[i]*=m;t->last_items+=2u;}
  t->cursor=end;if(end==257u){w->fft_im[0]=w->fft_im[256]=0.;t->cursor=257;t->phase=M16_CT_CONJUGATE;}return 1;
 }
 if(t->phase==M16_CT_CONJUGATE){
  end=t->cursor+32u;if(end>512u)end=512u;
  for(i=t->cursor;i<end;i++){w->fft_re[i]=w->fft_re[512u-i];w->fft_im[i]=-w->fft_im[512u-i];t->last_items+=2u;}
  t->cursor=end;if(end==512u){rc=m16_complex_fft_begin(&t->fft,512,1);if(rc)return fail(t,rc);t->phase=M16_CT_IFFT;}return 1;
 }
 if(t->phase==M16_CT_OLA){
  end=t->cursor+32u;if(end>512u)end=512u;
  for(i=t->cursor;i<end;i++){unsigned pos=t->frame*256u+i;double h=m16_periodic_hann512[i];w->signal[pos]+=w->fft_re[i]*h;w->norm[pos]+=h*h;t->last_items+=2u;}
  t->cursor=end;
  if(end==512u){t->cursor=0;if(++t->frame==t->frames)t->phase=M16_CT_CROP;else t->phase=M16_CT_MASKS;}return 1;
 }
 if(t->phase==M16_CT_CROP){
  end=t->cursor+64u;if(end>t->samples)end=t->samples;
  for(i=t->cursor;i<end;i++){
   w->wdas[i]=w->resampled[t->selection+i];
   if(t->ordinal<t->id_count){unsigned pos=256u+t->selection+i;if(w->norm[pos]<1e-8)return fail(t,M16_CT_OUTPUT);w->enhanced[i]=w->signal[pos]/w->norm[pos];if(!finite64(w->enhanced[i]))return fail(t,M16_CT_OUTPUT);}
   if(!finite64(w->wdas[i]))return fail(t,M16_CT_OUTPUT);
   t->last_items++;
  }
  t->cursor=end;if(end==t->samples)return output_ready(t);return 1;
 }
 return fail(t,M16_CT_INVALID);
}
int m16_continuous_task_step(m16_continuous_task *t){int rc;if(!valid(t))return M16_CT_INVALID;t->last_items=0;t->steps++;rc=step(t);if(t->last_items>t->max_items)t->max_items=t->last_items;return rc;}
int m16_continuous_task_output(const m16_continuous_task *t,const m16_cont_info **info,const double **a,const double **b,unsigned *n){
 if(!valid(t) || !info || !a || !b || !n)return M16_CT_INVALID;
 if(t->phase!=M16_CT_OUTPUT_READY)return 0;
 *info=&t->info;*a=t->work->wdas;*b=t->ordinal==t->id_count?t->work->wdas:t->work->enhanced;*n=t->samples;return 1;
}
int m16_continuous_task_release(m16_continuous_task *t,uint32_t epoch,uint32_t window,uint32_t id){
 if(!valid(t) || t->phase!=M16_CT_OUTPUT_READY || t->info.epoch!=epoch || t->info.window!=window || t->info.source_id!=id)return M16_CT_INVALID;
 if(t->ordinal==t->id_count){t->work->emitted++;if(t->final)t->work->closed=1;t->result_count=(int)t->id_count;t->phase=M16_CT_COMPLETE;}
 else{t->ordinal++;t->phase=M16_CT_SOURCE;}
 return 0;
}
int m16_continuous_task_stop(m16_continuous_task *t){
 if(!valid(t))return M16_CT_INVALID;
 (void)m16_complex_fft_stop(&t->fft);(void)m16_resample_task_stop(&t->resample);t->work->failed=1;t->phase=M16_CT_STOPPED;return 1;
}
