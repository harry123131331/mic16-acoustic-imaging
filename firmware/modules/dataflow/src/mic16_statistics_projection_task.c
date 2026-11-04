#include "mic16_statistics_projection_task.h"
#include "mic16_time_map.h"
#include <math.h>
#include <string.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d535431u
#define PI 3.14159265358979323846264338327950288
static int valid(const m16_statistics_projection_task *t){return t && !((uintptr_t)t%_Alignof(m16_statistics_projection_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static double minimum(double x){return x>1e-20?x:1e-20;}
static unsigned ca(unsigned p){return p<16?p:(p<24?p-16:p-24);}
static unsigned cb(unsigned p){return p<16?(p+1u)%16u:(p<24?p-8:p-20);}
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
static uint64_t pack(double re,double im){
 union{float f;uint32_t u;}a,b;a.f=(float)re;b.f=(float)im;
 if(!(a.u&0x7f800000u))a.u&=0x80000000u;
 if(!(b.u&0x7f800000u))b.u&=0x80000000u;
 return (uint64_t)a.u|((uint64_t)b.u<<32);
}
static double part(uint64_t v,unsigned imaginary){union{float f;uint32_t u;}a;a.u=(uint32_t)(imaginary?v>>32:v);return (double)a.f;}
static int fail(m16_statistics_projection_task *t,int error){
 if(!t->error)t->error=error;
 if(!t->owned&&t->runtime->arbitrated)(void)m16_projection_runtime_unclaim(t->runtime,t);
 t->phase=t->owned?M16_ST_DRAIN:M16_ST_FAILED;return t->owned?1:t->error;
}
static double *pairs(m16_statistics_projection_task *t,unsigned buffer){return buffer?t->storage->pairs_next:t->work->physical.statistics_cache.pairs;}
static unsigned block_frames(unsigned first){return 67u-first<4u?67u-first:4u;}
static double *band(m16_statistics_projection_task *t,unsigned f,unsigned c,unsigned z){return t->storage->bands+((f*16u+c)*64u+z)*5u;}
int m16_statistics_projection_init(m16_statistics_projection_task *t,m16_projection_runtime *r,mic16_extended_workspace *w,m16_statistics_projection_storage *s){
 const void *p[5];uintptr_t n[5];unsigned i,j;
 if(!t || !r || !w || !s || (uintptr_t)t%_Alignof(m16_statistics_projection_task) || (uintptr_t)r%_Alignof(m16_projection_runtime) ||
  (uintptr_t)w%_Alignof(mic16_extended_workspace) || (uintptr_t)s%_Alignof(m16_statistics_projection_storage) || t->magic ||
  !m16_projection_runtime_available(r) || r->words<M16_ST_ARENA_WORDS || !environment())return M16_ST_INVALID;
 p[0]=t;p[1]=r;p[2]=r->arena;p[3]=w;p[4]=s;n[0]=sizeof(*t);n[1]=sizeof(*r);n[2]=(uintptr_t)r->words*8u;n[3]=sizeof(*w);n[4]=sizeof(*s);
 for(i=0;i<5;i++)for(j=i+1;j<5;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_ST_INVALID;
 *t=(m16_statistics_projection_task){0};t->magic=MAGIC;t->runtime=r;t->work=w;t->storage=s;t->reserved_tag_high=r->tag_high;return 0;
}
int m16_statistics_projection_begin(m16_statistics_projection_task *t,const double *d,unsigned count,float *out,unsigned values,uint64_t epoch,uint64_t tag){
 unsigned i;const void *p[4];uintptr_t n[4];
 if(!valid(t) || !d || !out || (uintptr_t)d%8u || (uintptr_t)out%4u || count<1 || count>16 || values<count*20u*18u ||
  !epoch || epoch<t->last_epoch || !tag || tag>UINT64_MAX-135u || tag<=t->reserved_tag_high || (!t->runtime->arbitrated&&tag<=t->runtime->tag_high))return M16_ST_INVALID;
 if(!m16_statistics_projection_releasable(t))return M16_ST_BUSY;
 if(!environment())return M16_ST_FLOAT_ENV;
 p[0]=t;p[1]=t->runtime;p[2]=t->runtime->arena;p[3]=t->storage;
 n[0]=sizeof(*t);n[1]=sizeof(*t->runtime);n[2]=(uintptr_t)t->runtime->words*8u;n[3]=sizeof(*t->storage);
 for(i=0;i<4;i++)if(!apart(d,count*16u,p[i],n[i]) || !apart(out,count*360u*4u,p[i],n[i]))return M16_ST_INVALID;
 if(!apart(d,count*16u,t->work,sizeof(*t->work)) || !apart(d,count*16u,out,count*360u*4u) ||
  (out!=t->work->statistics18 && !apart(out,count*360u*4u,t->work,sizeof(*t->work))))return M16_ST_INVALID;
 for(i=0;i<count*2u;i++)if(!finite64(d[i]) || d[i]<=-90. || d[i]>=90.)return M16_ST_INPUT;
 for(i=0;i<count*2u;i++)t->directions[i]=d[i];
 t->count=count;t->output=out;t->epoch=t->last_epoch=epoch;t->first_tag=tag;t->reserved_tag_high=tag+135u;
 t->phase=M16_ST_VALIDATE;t->candidate=t->cursor=t->region=t->frame_start=t->buffer=t->next_cursor=t->owned=0;t->frames=4;
 t->error=0;t->steps=0;t->transactions=t->rotations=t->pair_normalizations=t->prefetch_pairs=t->device_prefetch_pairs=t->stop_requested=t->last_items=t->max_items=t->max_used=0;return 0;
}
static int normalize(m16_statistics_projection_task *t,unsigned buffer,unsigned first,unsigned *cursor,unsigned prefetch){
 unsigned i,begin=*cursor,end=begin+32u,n=block_frames(first)*256u*32u;double *out=pairs(t,buffer);const double *x=t->work->observed;
 if(end>n)end=n;
 for(i=*cursor;i<end;i++){
  unsigned p=i%32u,b=(i/32u)%256u+1u,f=first+i/(256u*32u),a=((f*257u+b)*16u+ca(p))*2u,q=((f*257u+b)*16u+cb(p))*2u;
  double ar=x[a],ai=x[a+1u],br=x[q],bi=x[q+1u],re,im,mag;
  if(!finite64(ar) || !finite64(ai) || !finite64(br) || !finite64(bi))return fail(t,M16_ST_INPUT);
  re=ar*br+ai*bi;im=ai*br-ar*bi;mag=hypot(re,im);
  if(!finite64(mag))return fail(t,M16_ST_INPUT);
  if(mag>1e-8){re/=mag;im/=mag;}else{re=0.;im=0.;}
  out[i*2u]=re;out[i*2u+1u]=im;t->pair_normalizations++;
  if(prefetch){t->prefetch_pairs++;if(t->runtime->phase==M16_PR_DEVICE)t->device_prefetch_pairs++;}
 }
 *cursor=end;t->last_items+=end-begin;
 return 1;
}
static void layout(m16_statistics_projection_task *t){
 t->obs=64u;t->coef=t->obs+t->frames*32u*32u;
 t->out=t->coef+((t->count+3u)/4u)*32u*32u*4u;
 t->receipt=(t->out+t->frames*32u*t->count*2u+7u)&~7u;t->used=t->receipt+8u;t->cursor=0;
 if(t->used>t->max_used)t->max_used=t->used;
}
static int prefetch(m16_statistics_projection_task *t){
 unsigned first=t->frame_start+4u;
 if(first<67u && t->next_cursor<block_frames(first)*256u*32u)return normalize(t,t->buffer^1u,first,&t->next_cursor,1);
 return 1;
}
static int drain(m16_statistics_projection_task *t){
 int rc;m16_projection_runtime *r=t->runtime;
 if(!t->owned){t->phase=t->error?M16_ST_FAILED:M16_ST_STOPPED;return t->error?t->error:2;}
 if(r->phase==M16_PR_QUARANTINE){t->phase=M16_ST_FAILED;if(!t->error)t->error=r->report.error;return t->error;}
 if(r->phase==M16_PR_COMPLETE){
  rc=m16_projection_runtime_retire_for(r,t);if(rc){t->phase=M16_ST_FAILED;if(!t->error)t->error=rc;return t->error;}
  t->owned=0;t->phase=t->error?M16_ST_FAILED:M16_ST_STOPPED;return t->error?t->error:2;
 }
 if(r->phase==M16_PR_PREPARE){
  rc=m16_projection_runtime_cancel_for(r,t);if(rc==1){t->owned=0;t->phase=t->error?M16_ST_FAILED:M16_ST_STOPPED;return t->error?t->error:2;}
  if(rc<0){t->phase=M16_ST_FAILED;if(!t->error)t->error=rc;return t->error;}
  return 1;
 }
 if(r->phase==M16_PR_IDLE){t->phase=M16_ST_FAILED;if(!t->error)t->error=M16_ST_OWNERSHIP;return t->error;}
 rc=m16_projection_runtime_poll_for(r,t);if(rc<0){t->phase=M16_ST_FAILED;if(!t->error)t->error=rc;return t->error;}return 1;
}
static void begin_rows(m16_statistics_projection_task *t){t->candidate=t->frame=t->zone=0;t->phase=M16_ST_ROWS;}
static int step(m16_statistics_projection_task *t){
 unsigned i,end;int rc;mic16_extended_workspace *w=t->work;mic16_statistics_reuse_storage *cache=&w->physical.statistics_cache;
 m16_projection_runtime *r=t->runtime;
 if(t->phase==M16_ST_COMPLETE)return 0;
 if(t->phase==M16_ST_STOPPED)return 2;
 if(t->phase==M16_ST_FAILED)return t->error;
 if(t->phase==M16_ST_IDLE)return M16_ST_INVALID;
 if(t->phase==M16_ST_DRAIN)return drain(t);
 if(!environment())return fail(t,M16_ST_FLOAT_ENV);
 if(!r->arbitrated&&!t->owned&&!m16_projection_runtime_available(r))return fail(t,M16_ST_OWNERSHIP);
 if(r->arbitrated&&t->phase>=M16_ST_PACK&&t->phase<=M16_ST_SUBMIT){
  rc=m16_projection_runtime_claim(r,t,1u);if(rc<0)return fail(t,rc);if(rc)return 1;
 }
 if(t->phase==M16_ST_VALIDATE){
  const double *x;unsigned n;
  if(t->region==0){x=w->observed;n=67u*257u*16u*2u;}
  else if(t->region==1){x=w->beam_power;n=t->count*67u*64u;}
  else if(t->region==2){x=w->channel_power;n=67u*64u;}else{x=w->quiet_power;n=64;}
  end=t->cursor+64u;if(end>n)end=n;
  for(i=t->cursor;i<end;i++){
   if(!finite64(x[i]) || (t->region==0?(x[i]<-8192. || x[i]>8192.):x[i]<0.))return fail(t,M16_ST_INPUT);
   t->last_items++;
  }
  t->cursor=end;if(end==n){t->cursor=0;if(++t->region==4u){t->candidate=0;t->phase=M16_ST_GEOMETRY;}}return 1;
 }
 if(t->phase==M16_ST_GEOMETRY){
  float lags[32];if(mic16_candidate_geometry(t->directions+t->candidate*2u,t->arrivals,lags))return fail(t,M16_ST_INPUT);
  t->cursor=0;t->phase=M16_ST_SIGNATURES;return 1;
 }
 if(t->phase==M16_ST_SIGNATURES){
  end=t->cursor+8u;if(end>257u*32u)end=257u*32u;
  for(i=t->cursor;i<end;i++){
   unsigned p=i%32u,b=i/32u,q=(t->candidate*257u*32u+i)*2u;
   double lag=(double)(t->arrivals[cb(p)]-t->arrivals[ca(p)])/39062.5,phase=2.*PI*(b*31.25)*lag;
   cache->signatures[q]=cos(phase);cache->signatures[q+1u]=sin(phase);t->last_items++;
  }
  t->cursor=end;if(end==257u*32u){t->cursor=0;if(++t->candidate==t->count){t->frame_start=t->buffer=0;t->frames=4;t->phase=M16_ST_PAIRS;}else t->phase=M16_ST_GEOMETRY;}return 1;
 }
 if(t->phase==M16_ST_PAIRS){
  rc=normalize(t,t->buffer,t->frame_start,&t->cursor,0);if(t->phase==M16_ST_FAILED)return rc;
  if(t->cursor==t->frames*256u*32u){t->cursor=0;t->phase=M16_ST_CLEAR;}return 1;
 }
 if(t->phase==M16_ST_CLEAR){
  end=t->cursor+64u;if(end>t->frames*t->count*64u*5u)end=t->frames*t->count*64u*5u;
  for(i=t->cursor;i<end;i++){unsigned f=i/(t->count*64u*5u),c=(i/(64u*5u))%t->count,z=(i/5u)%64u;band(t,f,c,z)[i%5u]=0.;t->last_items++;}
  t->cursor=end;if(end==t->frames*t->count*64u*5u){
   t->first_bin=1;t->cursor=0;
   if(t->count==1){t->frame=0;t->cursor=1;t->phase=M16_ST_CPU;}else{layout(t);t->phase=M16_ST_PACK;}
  }return 1;
 }
 if(t->phase==M16_ST_PACK){
  const double *p=pairs(t,t->buffer);end=t->cursor+64u;if(end>t->frames*32u*32u)end=t->frames*32u*32u;
  for(i=t->cursor;i<end;i++){
   unsigned f=i/(32u*32u),b=(i/32u)%32u,pair=i%32u,q=((f*256u+t->first_bin+b-1u)*32u+pair)*2u;
   r->arena[t->obs+i]=pack(p[q],p[q+1u]);t->last_items++;
  }
  t->cursor=end;if(end==t->frames*32u*32u){t->cursor=0;t->phase=M16_ST_COEFFICIENTS;}return 1;
 }
 if(t->phase==M16_ST_COEFFICIENTS){
  unsigned n=((t->count+3u)/4u)*32u*32u*4u;end=t->cursor+64u;if(end>n)end=n;
  for(i=t->cursor;i<end;i++){
   unsigned c=(i/(32u*32u*4u))*4u+i%4u,b=(i/(32u*4u))%32u,p=(i/4u)%32u;
   unsigned q=((c*257u+t->first_bin+b)*32u+p)*2u;
   r->arena[t->coef+i]=c<t->count?pack(cache->signatures[q],cache->signatures[q+1u]):0;t->last_items++;
  }
  t->cursor=end;if(end==n){t->cursor=0;t->phase=M16_ST_SUBMIT;}return 1;
 }
 if(t->phase==M16_ST_SUBMIT){
  uint64_t *d=r->arena+8u;unsigned no=t->frames*32u*32u,nc=((t->count+3u)/4u)*32u*32u*4u,nr=t->frames*32u*t->count*2u;
  d[0]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|(UINT64_C(1)<<48);
  d[1]=t->first_tag+(t->frame_start/4u)*8u+(t->first_bin-1u)/32u;d[2]=t->obs|((uint64_t)t->coef<<32);
  d[3]=t->out|((uint64_t)t->receipt<<32);d[4]=t->frames|(UINT64_C(32)<<16)|((uint64_t)t->count<<32);
  d[5]=nr;d[6]=no|((uint64_t)nc<<32);d[7]=0;
  rc=m16_projection_runtime_submit_for(r,t,8,1);if(rc==M16_PR_BUSY)return 1;if(rc)return fail(t,rc);
  t->owned=1;t->transactions++;t->phase=M16_ST_WAIT;return 1;
 }
 if(t->phase==M16_ST_WAIT){
  rc=m16_projection_runtime_poll_for(r,t);if(rc<0)return fail(t,rc);
  if(rc==0){t->cursor=0;t->phase=M16_ST_CONSUME;}
  return prefetch(t);
 }
 if(t->phase==M16_ST_CONSUME){
  if(r->phase!=M16_PR_COMPLETE)return fail(t,M16_ST_OWNERSHIP);
  end=t->cursor+16u;if(end>t->frames*32u*t->count)end=t->frames*32u*t->count;
  for(i=t->cursor;i<end;i++){
   unsigned f=i/(32u*t->count),b=(i/t->count)%32u,c=i%t->count,z=(t->first_bin+b-1u)/4u;
   uint64_t a=r->arena[t->out+i*2u],q=r->arena[t->out+i*2u+1u];double sr=part(a,0),si=part(a,1),lr=part(q,0),li=part(q,1),*v=band(t,f,c,z);
   if(!(sr>=-33. && sr<=33. && si>=-33. && si<=33. && lr>=-33. && lr<=33. && li>=-33. && li<=33.))return fail(t,M16_ST_OUTPUT);
   v[0]+=sr/16.;v[1]+=si/16.;v[2]+=lr/16.;v[3]+=li/16.;v[4]+=hypot((sr+lr)/32.,(si+li)/32.);t->last_items++;t->rotations++;
  }
  t->cursor=end;if(end==t->frames*32u*t->count)t->phase=M16_ST_RETIRE;return 1;
 }
 if(t->phase==M16_ST_RETIRE){
  rc=m16_projection_runtime_retire_for(r,t);if(rc)return fail(t,rc);t->owned=0;t->first_bin+=32u;
  if(t->first_bin>256u)begin_rows(t);else{layout(t);t->phase=M16_ST_PACK;}return 1;
 }
 if(t->phase==M16_ST_CPU){
  unsigned b=t->cursor,p;const double *x=pairs(t,t->buffer);double sr=0.,si=0.,lr=0.,li=0.,*v=band(t,t->frame,0,(b-1u)/4u);
  for(p=0;p<32u;p++){
   unsigned s=((t->frame*256u+b-1u)*32u+p)*2u,q=(b*32u+p)*2u;
   double re=x[s],im=x[s+1u],cr=re*cache->signatures[q]+im*cache->signatures[q+1u],ci=im*cache->signatures[q]-re*cache->signatures[q+1u];
   if(p<16u){sr+=cr;si+=ci;}else{lr+=cr;li+=ci;}t->last_items++;
  }
  v[0]+=sr/16.;v[1]+=si/16.;v[2]+=lr/16.;v[3]+=li/16.;v[4]+=hypot((sr+lr)/32.,(si+li)/32.);t->rotations++;
  if(++t->cursor==257u){t->cursor=1;if(++t->frame==t->frames)begin_rows(t);}return prefetch(t);
 }
 if(t->phase==M16_ST_ROWS){
  unsigned frame=t->frame_start+t->frame,z=t->zone,c=t->candidate,k;
  float *row=w->spectral10+(frame*64u+z)*10u;
  double power=w->beam_power[(c*67u+frame)*64u+z],channel=w->channel_power[frame*64u+z],quiet=w->quiet_power[z],other=power,*v=band(t,t->frame,c,z);
  if(!finite64(power) || !finite64(channel) || !finite64(quiet) || power<0. || channel<0. || quiet<0.)return fail(t,M16_ST_INPUT);
  for(k=0;k<5;k++)row[k]=(float)(v[k]/4.);
  if(t->count>1){
   other=0.;for(i=0;i<t->count;i++){
    double p=w->beam_power[(i*67u+frame)*64u+z];if(!finite64(p) || p<0.)return fail(t,M16_ST_INPUT);
    if(i!=c && p>other)other=p;
   }
  }
  row[5]=(float)(clip(10.*log10(minimum(power)),-100.,80.)/40.);
  row[6]=(float)(clip(10.*log10(minimum(power)/minimum(channel)),-40.,20.)/20.);
  row[7]=(float)(clip(10.*log10(minimum(power)/minimum(other)),-40.,20.)/20.);
  row[8]=(float)(clip(10.*log10(minimum(power)/minimum(quiet)),-20.,40.)/20.);
  row[9]=(float)((z*4+2.5)/256.);t->last_items=10;
  if(++t->zone==64u){t->cursor=t->feature=0;t->mean=0.f;t->phase=M16_ST_MEAN;}return 1;
 }
 if(t->phase==M16_ST_MEAN){
  unsigned frame=t->frame_start+t->frame;end=t->cursor+32u;if(end>64u)end=64u;
  for(i=t->cursor;i<end;i++){t->mean+=w->spectral10[(frame*64u+i)*10u+t->feature];t->last_items++;}
  t->cursor=end;if(end==64u){t->mean/=64.f;t->variance=0.f;t->cursor=0;t->phase=M16_ST_VARIANCE;}return 1;
 }
 if(t->phase==M16_ST_VARIANCE){
  unsigned frame=t->frame_start+t->frame;end=t->cursor+32u;if(end>64u)end=64u;
  for(i=t->cursor;i<end;i++){
   float d=w->spectral10[(frame*64u+i)*10u+t->feature]-t->mean;t->variance+=d*d;t->last_items++;
  }
  t->cursor=end;if(end==64u){
   unsigned q=(t->candidate*67u+frame)*18u+t->feature;
   cache->summaries[q]=t->mean;cache->summaries[q+9u]=sqrtf(t->variance/64.f);
   t->cursor=0;t->mean=0.f;
   if(++t->feature==9u){
    t->zone=0;t->phase=M16_ST_ROWS;
    if(++t->frame==t->frames){t->frame=0;if(++t->candidate==t->count)t->phase=M16_ST_ADVANCE;}
   }else t->phase=M16_ST_MEAN;
  }return 1;
 }
 if(t->phase==M16_ST_ADVANCE){
  if(t->frame_start+t->frames==67u){
   t->candidate=t->time_frame=t->feature=t->cursor=t->time_count=0;t->time_sum=0.f;t->phase=M16_ST_TIME;return 1;
  }
  t->frame_start+=4u;t->frames=block_frames(t->frame_start);t->buffer^=1u;t->cursor=t->next_cursor;t->next_cursor=0;
  if(t->cursor==t->frames*256u*32u){t->cursor=0;t->phase=M16_ST_CLEAR;}else t->phase=M16_ST_PAIRS;return 1;
 }
 if(t->phase==M16_ST_TIME){
  end=t->cursor+32u;if(end>67u)end=67u;
  for(i=t->cursor;i<end;i++){
   if(m16_stft_to_frame[i]==t->time_frame){t->time_sum+=cache->summaries[(t->candidate*67u+i)*18u+t->feature];t->time_count++;}
   t->last_items++;
  }
  t->cursor=end;if(end==67u){
   if(!t->time_count)return fail(t,M16_ST_OUTPUT);
   t->output[(t->candidate*20u+t->time_frame)*18u+t->feature]=t->time_sum/(float)t->time_count;
   t->time_sum=0.f;t->time_count=t->cursor=0;
   if(++t->feature==18u){t->feature=0;if(++t->time_frame==20u){t->time_frame=0;if(++t->candidate==t->count){t->region=0;t->phase=M16_ST_FINAL_COPY;}}}
  }return 1;
 }
 if(t->phase==M16_ST_FINAL_COPY){
  unsigned n=t->region?67u*18u:257u*32u*2u;end=t->cursor+64u;if(end>n)end=n;
  for(i=t->cursor;i<end;i++){
   if(t->region)w->summary18[i]=cache->summaries[(t->count-1u)*67u*18u+i];
   else w->signature[i]=cache->signatures[(t->count-1u)*257u*32u*2u+i];
   t->last_items++;
  }
  t->cursor=end;if(end==n){t->cursor=0;if(++t->region==2u)t->phase=M16_ST_CHECK;}return 1;
 }
 if(t->phase==M16_ST_CHECK){
  end=t->cursor+64u;if(end>t->count*20u*18u)end=t->count*20u*18u;
  for(i=t->cursor;i<end;i++){if(!finite64(t->output[i]))return fail(t,M16_ST_OUTPUT);t->last_items++;}
  t->cursor=end;if(end==t->count*20u*18u){t->phase=M16_ST_COMPLETE;return 0;}return 1;
 }
 return fail(t,M16_ST_INVALID);
}
int m16_statistics_projection_step(m16_statistics_projection_task *t){
 int rc;if(!valid(t))return M16_ST_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_statistics_projection_stop(m16_statistics_projection_task *t){
 if(!valid(t))return M16_ST_INVALID;
 if(!t->owned&&t->runtime->arbitrated)(void)m16_projection_runtime_unclaim(t->runtime,t);
 if(m16_statistics_projection_releasable(t)){t->phase=M16_ST_STOPPED;return 1;}
 if(t->phase==M16_ST_FAILED)return t->error;
 t->stop_requested=1;t->phase=M16_ST_DRAIN;
 if(!t->owned){t->phase=t->error?M16_ST_FAILED:M16_ST_STOPPED;return t->error?t->error:1;}
 return 0;
}
int m16_statistics_projection_releasable(const m16_statistics_projection_task *t){
 return valid(t) && !t->owned && (t->runtime->arbitrated?!m16_projection_runtime_owns(t->runtime,t):m16_projection_runtime_available(t->runtime)) &&
  (t->phase==M16_ST_IDLE || t->phase==M16_ST_COMPLETE || t->phase==M16_ST_STOPPED || t->phase==M16_ST_FAILED);
}
