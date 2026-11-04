#include "mic16_enh_feature_task.h"
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d454631u
#define PI 3.14159265358979323846264338327950288
static int valid(const m16_enh_feature_task *t){return t && !((uintptr_t)t%_Alignof(m16_enh_feature_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);}
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
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
static int fail(m16_enh_feature_task *t,int rc){t->error=rc;t->phase=M16_EF_FAILED;return rc;}
static unsigned ca(unsigned p){return p<16u?p:(p<24u?p-16u:p-24u);}
static unsigned cb(unsigned p){return p<16u?(p+1u)%16u:(p<24u?p-8u:p-20u);}
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static double power_floor(double x){return x>1e-20?x:1e-20;}
int m16_enh_feature_init(m16_enh_feature_task *t,mic16_extended_workspace *w){
 if(!t || (uintptr_t)t%_Alignof(m16_enh_feature_task) || t->magic || !w || (uintptr_t)w%_Alignof(mic16_extended_workspace) || !apart(t,sizeof(*t),w,sizeof(*w)) || !environment())return M16_EF_INVALID;
 *t=(m16_enh_feature_task){0};t->magic=MAGIC;t->work=w;return 0;
}
int m16_enh_feature_releasable(const m16_enh_feature_task *t){return valid(t) && (t->phase==M16_EF_IDLE || t->phase==M16_EF_COMPLETE || t->phase==M16_EF_STOPPED || t->phase==M16_EF_FAILED);}
int m16_enh_feature_begin(m16_enh_feature_task *t,const double *d,const unsigned char *bits,unsigned c){
 float arrivals[16],lags[32];unsigned i;
 if(!valid(t) || !d || (uintptr_t)d%8u || !bits || c>=16u || !apart(d,256u,t,sizeof(*t)) || !apart(bits,16u,t,sizeof(*t)) ||
  !apart(d,256u,bits,16u) || !apart(d,256u,t->work,sizeof(*t->work)) || !apart(bits,16u,t->work,sizeof(*t->work)))return M16_EF_INVALID;
 if(!m16_enh_feature_releasable(t))return M16_EF_BUSY;
 if(!environment())return M16_EF_FLOAT_ENV;
 for(i=0;i<16;i++)if(bits[i]>1u || !finite64(d[i*2u]) || !finite64(d[i*2u+1u]))return M16_EF_INPUT;
 if(!bits[c] || mic16_candidate_geometry(d+c*2u,arrivals,lags))return M16_EF_INPUT;
 for(i=0;i<16;i++){t->arrivals[i]=arrivals[i];t->valid[i]=bits[i];}
 t->candidate=c;t->frame=t->bin=t->pair=t->cursor=t->last_items=t->max_items=0;t->steps=0;t->error=0;t->phase=M16_EF_SIGNATURE;return 0;
}
static int step(m16_enh_feature_task *t){
 unsigned i,end;mic16_extended_workspace *w=t->work;
 if(t->phase==M16_EF_COMPLETE)return 0;
 if(t->phase==M16_EF_STOPPED)return 2;
 if(t->phase==M16_EF_FAILED)return t->error;
 if(t->phase==M16_EF_IDLE)return M16_EF_INVALID;
 if(!environment())return fail(t,M16_EF_FLOAT_ENV);
 if(t->phase==M16_EF_SIGNATURE){
  end=t->cursor+8u;if(end>257u*32u)end=257u*32u;
  for(i=t->cursor;i<end;i++){
   unsigned b=i/32u,p=i%32u;
   double lag=(double)(t->arrivals[cb(p)]-t->arrivals[ca(p)])/39062.5;
   double phase=2.*PI*(double)b*31.25*lag;
   w->signature[i*2u]=cos(phase);w->signature[i*2u+1u]=sin(phase);t->last_items++;
  }
  t->cursor=end;if(end==257u*32u){t->cursor=0;t->phase=M16_EF_CLEAR;}return 1;
 }
 if(t->phase==M16_EF_CLEAR){
  end=t->cursor+64u;if(end>64u*5u)end=64u*5u;
  for(i=t->cursor;i<end;i++){t->band[i/5u][i%5u]=0.;t->last_items++;}
  t->cursor=end;if(end==64u*5u){t->bin=1;t->pair=0;t->sr=t->si=t->lr=t->li=0.;t->phase=M16_EF_PAIRS;}return 1;
 }
 if(t->phase==M16_EF_PAIRS){
  end=t->pair+8u;
  for(i=t->pair;i<end;i++){
   unsigned a=((t->frame*257u+t->bin)*16u+ca(i))*2u,q=((t->frame*257u+t->bin)*16u+cb(i))*2u,v=(t->bin*32u+i)*2u;
   double ar=w->observed[a],ai=w->observed[a+1u],br=w->observed[q],bi=w->observed[q+1u];
   double re,im,mag,cr,ci;
   if(!finite64(ar) || !finite64(ai) || !finite64(br) || !finite64(bi))return fail(t,M16_EF_INPUT);
   re=ar*br+ai*bi;im=ai*br-ar*bi;mag=hypot(re,im);
   if(!finite64(mag))return fail(t,M16_EF_INPUT);
   if(mag>1e-8){re/=mag;im/=mag;}else{re=0.;im=0.;}
   cr=re*w->signature[v]+im*w->signature[v+1u];ci=im*w->signature[v]-re*w->signature[v+1u];
   if(i<16u){t->sr+=cr;t->si+=ci;}else{t->lr+=cr;t->li+=ci;}
   t->last_items++;
  }
  t->pair=end;if(end==32u)t->phase=M16_EF_BAND;return 1;
 }
 if(t->phase==M16_EF_BAND){
  unsigned z=(t->bin-1u)/4u;
  t->band[z][0]+=t->sr/16.;t->band[z][1]+=t->si/16.;t->band[z][2]+=t->lr/16.;t->band[z][3]+=t->li/16.;
  t->band[z][4]+=hypot((t->sr+t->lr)/32.,(t->si+t->li)/32.);t->last_items=5;
  if(++t->bin==257u){t->cursor=0;t->phase=M16_EF_OUTPUT;}
  else{t->pair=0;t->sr=t->si=t->lr=t->li=0.;t->phase=M16_EF_PAIRS;}return 1;
 }
 if(t->phase==M16_EF_OUTPUT){
  unsigned z=t->cursor,k,peers=0;double power=w->beam_power[(t->candidate*67u+t->frame)*64u+z],other=0.;
  double channel=w->channel_power[t->frame*64u+z],quiet=w->quiet_power[z];float *f=w->spectral10+(t->frame*64u+z)*10u;
  if(!finite64(power) || power<0. || !finite64(channel) || channel<0. || !finite64(quiet) || quiet<0.)return fail(t,M16_EF_INPUT);
  for(k=0;k<5u;k++)f[k]=(float)(t->band[z][k]/4.);
  for(i=0;i<16u;i++)if(t->valid[i] && i!=t->candidate){
   double v=w->beam_power[(i*67u+t->frame)*64u+z];if(!finite64(v) || v<0.)return fail(t,M16_EF_INPUT);
   if(v>other)other=v;
   peers++;
  }
  if(!peers)other=power;
  f[5]=(float)(clip(10.*log10(power_floor(power)),-100.,80.)/40.);
  f[6]=(float)(clip(10.*log10(power_floor(power)/power_floor(channel)),-40.,20.)/20.);
  f[7]=(float)(clip(10.*log10(power_floor(power)/power_floor(other)),-40.,20.)/20.);
  f[8]=(float)(clip(10.*log10(power_floor(power)/power_floor(quiet)),-20.,40.)/20.);
  f[9]=(float)((z*4u+2.5)/256.);
  for(k=0;k<10u;k++)if(!finite64(f[k]))return fail(t,M16_EF_OUTPUT_ERROR);
  t->last_items=26;
  if(++t->cursor==64u){
   if(++t->frame==67u){t->phase=M16_EF_COMPLETE;return 0;}
   t->cursor=0;t->phase=M16_EF_CLEAR;
  }
  return 1;
 }
 return fail(t,M16_EF_INVALID);
}
int m16_enh_feature_step(m16_enh_feature_task *t){
 int rc;if(!valid(t))return M16_EF_INVALID;t->last_items=0;t->steps++;rc=step(t);if(t->last_items>t->max_items)t->max_items=t->last_items;return rc;
}
int m16_enh_feature_stop(m16_enh_feature_task *t){if(!valid(t))return M16_EF_INVALID;t->phase=M16_EF_STOPPED;return 1;}
