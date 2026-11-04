#include "mic16_enh_prediction_task.h"
#define MAGIC 0x4d455031u
static int valid(const m16_enh_prediction_task *t){return t && !((uintptr_t)t%_Alignof(m16_enh_prediction_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);}
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static void children_stop(m16_enh_prediction_task *t){
 (void)m16_channel_weights_stop(&t->weights);(void)m16_weighted_beam_stop(&t->beam);
 (void)m16_enh_spectrum_stop(&t->spectrum);(void)m16_enh_feature_stop(&t->feature);(void)m16_enh_mask_task_stop(&t->mask);
}
static int fail(m16_enh_prediction_task *t,int rc){
 children_stop(t);t->prediction_valid=0;if(t->prediction)t->prediction->count=0;t->phase=M16_EP_FAILED;t->error=rc;return rc;
}
int m16_enh_prediction_init(m16_enh_prediction_task *t,mic16_extended_workspace *s,m16_enh_workspace *w){
 if(!t || (uintptr_t)t%_Alignof(m16_enh_prediction_task) || t->magic || !s || !w ||
  (uintptr_t)s%_Alignof(mic16_extended_workspace) || (uintptr_t)w%_Alignof(m16_enh_workspace) ||
  !apart(t,sizeof(*t),s,sizeof(*s)) || !apart(t,sizeof(*t),w,sizeof(*w)) || !apart(s,sizeof(*s),w,sizeof(*w)))return M16_EP_INVALID;
 *t=(m16_enh_prediction_task){0};
 if(m16_channel_weights_init(&t->weights,w) || m16_weighted_beam_init(&t->beam) || m16_enh_spectrum_init(&t->spectrum,s) ||
  m16_enh_feature_init(&t->feature,s) || m16_enh_mask_task_init(&t->mask,&w->network))return M16_EP_INVALID;
 t->magic=MAGIC;t->shared=s;t->work=w;return 0;
}
int m16_enh_prediction_releasable(const m16_enh_prediction_task *t){
 return valid(t) && (t->phase==M16_EP_IDLE || t->phase==M16_EP_COMPLETE || t->phase==M16_EP_STOPPED || t->phase==M16_EP_FAILED) &&
  m16_channel_weights_releasable(&t->weights) && m16_weighted_beam_releasable(&t->beam) && m16_enh_spectrum_releasable(&t->spectrum) &&
  m16_enh_feature_releasable(&t->feature) && m16_enh_mask_task_releasable(&t->mask);
}
int m16_enh_prediction_begin(m16_enh_prediction_task *t,const float *pcm,const float *quiet,const double *d,const unsigned char *bits,
 const mic16_cycle_output *tracks,m16_cont_prediction *out){
 unsigned i,j,c,n=0,selected[8],ids[8];const void *p[9];uintptr_t size[9];
 if(!valid(t) || !pcm || !quiet || !d || !bits || !tracks || !out || (uintptr_t)pcm%4u || (uintptr_t)quiet%4u || (uintptr_t)d%8u ||
  (uintptr_t)tracks%_Alignof(mic16_cycle_output) || (uintptr_t)out%_Alignof(m16_cont_prediction))return M16_EP_INVALID;
 p[0]=t;p[1]=pcm;p[2]=quiet;p[3]=d;p[4]=bits;p[5]=tracks;p[6]=out;p[7]=t->shared;p[8]=t->work;
 size[0]=sizeof(*t);size[1]=40960u*16u*4u;size[2]=8192u*16u*4u;size[3]=256;size[4]=16;size[5]=sizeof(*tracks);size[6]=sizeof(*out);size[7]=sizeof(*t->shared);size[8]=sizeof(*t->work);
 for(i=0;i<9;i++)for(j=i+1;j<9;j++)if(!apart(p[i],size[i],p[j],size[j]))return M16_EP_INVALID;
 if(!m16_enh_prediction_releasable(t))return M16_EP_BUSY;
 if(tracks->count<0 || tracks->count>8 || tracks->confirmed_count<0 || tracks->confirmed_count>tracks->count)return M16_EP_INPUT;
 for(c=0;c<16;c++){
  if(bits[c]>1 || !finite64(d[c*2]) || !finite64(d[c*2+1]))return M16_EP_INPUT;
  if(bits[c] && (d[c*2]<=-90. || d[c*2]>=90. || d[c*2+1]<=-90. || d[c*2+1]>=90.))return M16_EP_INPUT;
 }
 for(i=0;i<(unsigned)tracks->count;i++){
  const mic16_track *tr=tracks->tracks+i;double best=1e300;int choice=-1;
  if(!tr->source_id || (tr->confirmed_this_cycle!=0 && tr->confirmed_this_cycle!=1))return M16_EP_INPUT;
  for(j=0;j<i;j++)if(tracks->tracks[j].source_id==tr->source_id)return M16_EP_INPUT;
  if(!tr->confirmed_this_cycle)continue;
  if(!finite64(tr->score) || tr->score<.65 || tr->score>1. || !finite64(tr->azimuth) || !finite64(tr->elevation))return M16_EP_INPUT;
  for(c=0;c<16;c++)if(bits[c]){
   double dx=d[c*2]-tr->azimuth,dy=d[c*2+1]-tr->elevation,delta=dx*dx+dy*dy;
   if(delta<best){best=delta;choice=(int)c;}
  }
  if(choice<0 || best>1e-8)return M16_EP_INPUT;
  for(j=0;j<n;j++)if(selected[j]==(unsigned)choice)return M16_EP_INPUT;
  selected[n]=(unsigned)choice;ids[n]=tr->source_id;n++;
 }
 if(n!=(unsigned)tracks->confirmed_count)return M16_EP_INPUT;
 for(i=0;i<32;i++)t->directions[i]=d[i];
 for(i=0;i<16;i++)t->valid[i]=bits[i];
 for(i=0;i<n;i++){t->selected[i]=selected[i];t->source_ids[i]=ids[i];}
 t->selected_count=n;t->pcm=pcm;t->reference=quiet;t->prediction=out;out->count=0;t->prediction_valid=0;
 t->cursor=t->candidate=t->source=t->last_items=t->max_items=0;t->steps=0;t->error=0;t->phase=M16_EP_VALIDATE_PCM;return 0;
}
static int child(m16_enh_prediction_task *t,int rc,unsigned items){t->last_items=items;if(rc<0)return fail(t,rc);if(rc==2)return fail(t,M16_EP_INVALID);return rc;}
static int step(m16_enh_prediction_task *t){
 unsigned i,end;int rc;
 if(t->phase==M16_EP_COMPLETE)return 0;
 if(t->phase==M16_EP_STOPPED)return 2;
 if(t->phase==M16_EP_FAILED)return t->error;
 if(t->phase==M16_EP_IDLE)return M16_EP_INVALID;
 if(t->phase==M16_EP_VALIDATE_PCM || t->phase==M16_EP_VALIDATE_REFERENCE){
  unsigned n=t->phase==M16_EP_VALIDATE_PCM?40960u*16u:8192u*16u;const float *x=t->phase==M16_EP_VALIDATE_PCM?t->pcm:t->reference;
  end=t->cursor+64u;if(end>n)end=n;
  for(i=t->cursor;i<end;i++){if(!(x[i]>=-1.f && x[i]<=1.f))return fail(t,M16_EP_INPUT);t->last_items++;}
  t->cursor=end;
  if(end==n){
   t->cursor=0;
   if(t->phase==M16_EP_VALIDATE_PCM)t->phase=M16_EP_VALIDATE_REFERENCE;
   else if(!t->selected_count)t->phase=M16_EP_COPY_WEIGHTS;
   else{rc=m16_channel_weights_begin(&t->weights,t->pcm,40960u*16u);if(rc)return fail(t,rc);t->phase=M16_EP_WEIGHTS;}
  }
  return 1;
 }
 if(t->phase==M16_EP_WEIGHTS){
  rc=m16_channel_weights_step(&t->weights);rc=child(t,rc,t->weights.last_items);if(rc<0)return rc;
  if(!rc){rc=m16_enh_spectrum_begin32(&t->spectrum,M16_ES_OBSERVED16,t->pcm,40960u*16u);if(rc)return fail(t,rc);t->phase=M16_EP_OBSERVED;}return 1;
 }
 if(t->phase==M16_EP_OBSERVED || t->phase==M16_EP_REFERENCE || t->phase==M16_EP_BEAM_SPECTRUM){
  rc=m16_enh_spectrum_step(&t->spectrum);rc=child(t,rc,t->spectrum.last_items);if(rc<0)return rc;
  if(!rc){
   if(t->phase==M16_EP_OBSERVED){rc=m16_enh_spectrum_begin32(&t->spectrum,M16_ES_QUIET16,t->reference,8192u*16u);if(rc)return fail(t,rc);t->phase=M16_EP_REFERENCE;}
   else if(t->phase==M16_EP_REFERENCE){t->cursor=0;t->phase=M16_EP_COPY_WEIGHTS;}
   else{t->candidate++;t->phase=M16_EP_BEAM_BEGIN;}
  }
  return 1;
 }
 if(t->phase==M16_EP_COPY_WEIGHTS){
  end=t->cursor+64u;if(end>320u)end=320u;
  for(i=t->cursor;i<end;i++){t->prediction->weights[i]=t->selected_count?t->work->weights[i]:0.f;t->last_items++;}
  t->cursor=end;
  if(end==320u){
   if(!t->selected_count){t->prediction_valid=1;t->phase=M16_EP_COMPLETE;return 0;}
   t->candidate=0;t->phase=M16_EP_BEAM_BEGIN;
  }
  return 1;
 }
 if(t->phase==M16_EP_BEAM_BEGIN){
  while(t->candidate<16u && !t->valid[t->candidate])t->candidate++;
  if(t->candidate==16u){t->source=0;t->phase=M16_EP_FEATURE_BEGIN;return 1;}
  rc=m16_weighted_beam_begin(&t->beam,t->pcm,t->work->weights,t->directions+t->candidate*2u,t->work->beam);
  if(rc)return fail(t,rc);
  t->phase=M16_EP_BEAM;return 1;
 }
 if(t->phase==M16_EP_BEAM){
  rc=m16_weighted_beam_step(&t->beam);rc=child(t,rc,t->beam.last_items);if(rc<0)return rc;
  if(!rc){rc=m16_enh_spectrum_begin64(&t->spectrum,t->work->beam,t->candidate);if(rc)return fail(t,rc);t->phase=M16_EP_BEAM_SPECTRUM;}return 1;
 }
 if(t->phase==M16_EP_FEATURE_BEGIN){
  rc=m16_enh_feature_begin(&t->feature,t->directions,t->valid,t->selected[t->source]);if(rc)return fail(t,rc);
  t->phase=M16_EP_FEATURE;return 1;
 }
 if(t->phase==M16_EP_FEATURE){
  rc=m16_enh_feature_step(&t->feature);rc=child(t,rc,t->feature.last_items);if(rc<0)return rc;
  if(!rc){rc=m16_enh_mask_task_begin(&t->mask,t->shared->spectral10,67u*64u*10u,t->work->mask,67u*64u);if(rc)return fail(t,rc);t->phase=M16_EP_MASK;}return 1;
 }
 if(t->phase==M16_EP_MASK){
  rc=m16_enh_mask_task_step(&t->mask);rc=child(t,rc,t->mask.last_items);if(rc<0)return rc;
  if(!rc){
   unsigned c=t->selected[t->source];t->prediction->ids[t->source]=t->source_ids[t->source];
   t->prediction->directions[t->source][0]=t->directions[c*2u];t->prediction->directions[t->source][1]=t->directions[c*2u+1u];
   t->cursor=0;t->phase=M16_EP_COPY_MASK;
  }
  return 1;
 }
 if(t->phase==M16_EP_COPY_MASK){
  end=t->cursor+64u;if(end>67u*64u)end=67u*64u;
  for(i=t->cursor;i<end;i++){t->prediction->masks[t->source][i]=t->work->mask[i];t->last_items++;}
  t->cursor=end;
  if(end==67u*64u){
   if(++t->source==t->selected_count){t->prediction->count=t->selected_count;t->prediction_valid=1;t->phase=M16_EP_COMPLETE;return 0;}
   t->phase=M16_EP_FEATURE_BEGIN;
  }
  return 1;
 }
 return fail(t,M16_EP_INVALID);
}
int m16_enh_prediction_step(m16_enh_prediction_task *t){
 int rc;if(!valid(t))return M16_EP_INVALID;t->last_items=0;t->steps++;rc=step(t);if(t->last_items>t->max_items)t->max_items=t->last_items;return rc;
}
int m16_enh_prediction_stop(m16_enh_prediction_task *t){
 if(!valid(t))return M16_EP_INVALID;
 children_stop(t);t->prediction_valid=0;if(t->prediction)t->prediction->count=0;t->phase=M16_EP_STOPPED;return 1;
}
