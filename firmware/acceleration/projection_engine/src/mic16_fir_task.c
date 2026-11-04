#include "mic16_fir_task.h"
#include "mic16_fir_range_guard.h"
#include <string.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d464952u
#define ABI 2026102806u
enum {DESCRIPTORS=8,RECEIPT=16,INPUT=24,METADATA=2072,OUTPUT=2080};
static int valid(const m16_fir_task *t){return t&&!((uintptr_t)t%_Alignof(m16_fir_task))&&t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static uint64_t bits(double x){uint64_t u;memcpy(&u,&x,8);return u;}
static double number(uint64_t x){double v;memcpy(&v,&x,8);return v;}
static int finite_word(uint64_t x){return (x&UINT64_C(0x7fffffffffffffff))<UINT64_C(0x7ff0000000000000);}
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
static int fail(m16_fir_task *t,int rc){t->error=rc;t->phase=M16_FIR_FAILED;return rc;}
static int stopped(m16_fir_task *t){t->client=0;t->phase=M16_FIR_STOPPED;return 1;}
static double sample(const m16_fir_request *q,unsigned index){
 if(q->kind)return ((const double *)q->input)[index];
 if(q->channels==16u)return (double)((const float *)q->input)[index];
 return (double)((const float *)q->input)[index*q->stride+q->channel];
}
int m16_fir_task_init(m16_fir_task *t,m16_projection_runtime *r,unsigned abi,unsigned priority){
 if(!t||!r||(uintptr_t)t%_Alignof(m16_fir_task)||t->magic||priority>7u||
  !m16_projection_runtime_available(r)||!r->arbitrated||r->words<2608u||
  !apart(t,sizeof(*t),r,sizeof(*r))||!apart(t,sizeof(*t),r->arena,(uintptr_t)r->words*8u))return M16_FIR_INVALID;
 if(abi!=ABI||M16_PROJECTION_VERSION!=6u||M16_PROJECTION_BUILD_ABI!=ABI)return M16_FIR_ABI;
 if(!environment())return M16_FIR_FLOAT_ENV;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->runtime=r;t->priority=priority;return 0;
}
int m16_fir_task_owns(const m16_fir_task *t,const void *client){return valid(t)&&client&&t->client==client;}
int m16_fir_task_request(m16_fir_task *t,const void *client,const m16_fir_request *request){
 const void *p[5];uintptr_t bytes[5];unsigned i,j,length;m16_fir_request q;m16_projection_runtime *r;
 if(!valid(t)||!client||!request)return M16_FIR_INVALID;
 q=*request;r=t->runtime;
 if(!q.input||!q.output||q.kind>1u||!q.samples||q.samples>(q.kind?45680u:40960u)||
  (q.channels!=1u&&q.channels!=16u)||!q.stride||q.stride>16u||q.channel>=q.stride||
  (q.channels==16u&&(q.stride!=16u||q.channel))||
  (q.kind&&(q.stride!=1u||q.channel||q.channels!=1u))||
  (uintptr_t)q.input%(q.kind?8u:4u)||(uintptr_t)q.output%8u)return M16_FIR_INVALID;
 length=(q.samples*256u+624u)/625u;
 if(q.capacity<length*q.channels||t->requests==UINT64_MAX)return M16_FIR_INVALID;
 p[0]=t;bytes[0]=sizeof(*t);p[1]=r;bytes[1]=sizeof(*r);p[2]=r->arena;bytes[2]=(uintptr_t)r->words*8u;
 p[3]=q.input;bytes[3]=(uintptr_t)q.samples*q.stride*(q.kind?8u:4u);p[4]=q.output;bytes[4]=(uintptr_t)length*q.channels*8u;
 for(i=0;i<5;i++)for(j=i+1;j<5;j++)if(!apart(p[i],bytes[i],p[j],bytes[j]))return M16_FIR_INVALID;
 if(r->phase==M16_PR_QUARANTINE)return r->report.error;
 if(t->client)return 1;
 if(!environment())return M16_FIR_FLOAT_ENV;
 t->client=client;t->request=q;t->length=length;t->phase=M16_FIR_VALIDATE;
 t->cursor=t->first_output=t->cancel_requested=t->unsafe=t->last_items=t->max_items=0;
 t->error=0;t->requests++;return 0;
}
static int prepare_tile(m16_fir_task *t){
 unsigned time,low,high;
 t->tile_outputs=t->length-t->first_output;if(t->tile_outputs>32u)t->tile_outputs=32u;
 time=(t->first_output+10u)*625u;low=time<12500u?0u:(time-12500u+255u)/256u;
 high=(t->first_output+t->tile_outputs-1u+10u)*625u/256u;
 if(high>=t->request.samples)high=t->request.samples-1u;
 if(high<low||high-low+1u>128u)return fail(t,M16_FIR_INVALID);
 t->first_input=low;t->tile_inputs=high-low+1u;t->cursor=0;
 t->expected_high=t->runtime->tag_high;
 if(t->expected_high==UINT64_MAX)return fail(t,M16_FIR_INVALID);
 t->tag=t->expected_high+1u;t->phase=M16_FIR_PACK;return 1;
}
static int service(m16_fir_task *t){
 unsigned i,end;int rc;m16_projection_runtime *r;const m16_fir_request *q;
 if(!valid(t))return M16_FIR_INVALID;
 t->last_items=0;if(t->steps!=UINT64_MAX)t->steps++;
 r=t->runtime;q=&t->request;
 if(t->phase==M16_FIR_FAILED)return t->error;
 if(!t->client||t->phase==M16_FIR_COMPLETE||t->phase==M16_FIR_FALLBACK)return 0;
 if(!environment())return fail(t,M16_FIR_FLOAT_ENV);
 end=t->cursor+64u;
 if(t->phase==M16_FIR_VALIDATE){
  if(end>q->samples*q->channels)end=q->samples*q->channels;
  for(i=t->cursor;i<end;i++){
   double x=sample(q,i);uint64_t u=bits(x);
   if(!finite_word(u)||(!q->kind&&!(x>=-1.&&x<=1.)))return fail(t,M16_FIR_INPUT);
   if(!m16_fir_safe_word(u))t->unsafe=1u;
   t->last_items++;
  }
  t->cursor=end;
  if(end==q->samples*q->channels){t->cursor=0;t->phase=t->unsafe?M16_FIR_FALLBACK:M16_FIR_WAIT_SLOT;}
  return 1;
 }
 if(t->phase==M16_FIR_WAIT_SLOT){
  rc=m16_projection_runtime_claim(r,t,t->priority);
  if(rc<0)return fail(t,rc);
  return rc?1:prepare_tile(t);
 }
 if((t->phase==M16_FIR_PACK||t->phase==M16_FIR_SUBMIT)&&
   (!m16_projection_runtime_prepare_available(r,t)||r->tag_high!=t->expected_high))return fail(t,M16_FIR_OWNERSHIP);
 if(t->phase==M16_FIR_PACK){
  if(end>t->tile_inputs*q->channels)end=t->tile_inputs*q->channels;
  for(i=t->cursor;i<end;i++){
   double x=sample(q,t->first_input*q->channels+i);uint64_t u=bits(x);
   if(!finite_word(u)||(!q->kind&&!(x>=-1.&&x<=1.))||!m16_fir_safe_word(u))return fail(t,M16_FIR_INPUT);
   r->arena[INPUT+i]=u;t->last_items++;
  }
  t->cursor=end;
  if(end==t->tile_inputs*q->channels){
   r->arena[METADATA]=t->first_output|((uint64_t)t->first_input<<32);r->arena[METADATA+1]=q->samples;
   t->input_words+=(uint64_t)t->tile_inputs*q->channels;t->cursor=0;t->phase=M16_FIR_SUBMIT;
  }return 1;
 }
 if(t->phase==M16_FIR_SUBMIT){
  uint64_t *d=r->arena+DESCRIPTORS;
  d[0]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|(UINT64_C(5)<<48);d[1]=t->tag;
  d[2]=INPUT|((uint64_t)METADATA<<32);d[3]=OUTPUT|((uint64_t)RECEIPT<<32);
  d[4]=t->tile_outputs|((uint64_t)q->channels<<16)|((uint64_t)t->tile_inputs<<32);
  d[5]=t->tile_outputs*q->channels;d[6]=t->tile_inputs*q->channels|(UINT64_C(2)<<32);d[7]=0;
  rc=m16_projection_runtime_submit_for(r,t,DESCRIPTORS,1u);if(rc)return fail(t,rc);
  t->transactions++;t->phase=M16_FIR_DEVICE;return 1;
 }
 if(t->phase==M16_FIR_DEVICE){
  rc=m16_projection_runtime_poll_for(r,t);if(rc<0)return fail(t,rc);
  if(rc)return 1;
  t->cursor=0;t->phase=M16_FIR_CHECK;return 1;
 }
 if(t->phase==M16_FIR_CHECK||t->phase==M16_FIR_COPY){
  if(t->cancel_requested){
   rc=m16_projection_runtime_retire_for(r,t);if(rc)return fail(t,rc);
   (void)stopped(t);return 0;
  }
  if(end>t->tile_outputs*q->channels)end=t->tile_outputs*q->channels;
  for(i=t->cursor;i<end;i++){
   uint64_t u=r->arena[OUTPUT+i];
   if(!finite_word(u))return fail(t,M16_FIR_OUTPUT);
   if(t->phase==M16_FIR_COPY)q->output[t->first_output*q->channels+i]=number(u);
   t->last_items++;
  }
  t->cursor=end;
  if(end==t->tile_outputs*q->channels){
   t->cursor=0;
   if(t->phase==M16_FIR_CHECK)t->phase=M16_FIR_COPY;
   else{
    rc=m16_projection_runtime_retire_for(r,t);if(rc)return fail(t,rc);
    t->output_words+=(uint64_t)t->tile_outputs*q->channels;
    t->first_output+=t->tile_outputs;
    t->phase=t->first_output==t->length?M16_FIR_COMPLETE:M16_FIR_WAIT_SLOT;
   }
  }return 1;
 }
 return fail(t,M16_FIR_INVALID);
}
int m16_fir_task_service(m16_fir_task *t){
 int rc=service(t);
 if(valid(t)&&t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_fir_task_poll(m16_fir_task *t,const void *client){
 int rc;if(!m16_fir_task_owns(t,client))return M16_FIR_INVALID;
 rc=m16_fir_task_service(t);
 if(rc<0)return rc;
 if(t->phase==M16_FIR_FALLBACK){t->client=0;t->phase=M16_FIR_IDLE;return 2;}
 if(t->phase==M16_FIR_COMPLETE){t->client=0;t->phase=M16_FIR_IDLE;return 0;}
 return 1;
}
int m16_fir_task_cancel(m16_fir_task *t,const void *client){
 int rc;m16_projection_runtime *r;
 if(!valid(t)||!client)return M16_FIR_INVALID;
 if(t->client!=client)return 1;
 r=t->runtime;
 if(t->phase==M16_FIR_FALLBACK||t->phase==M16_FIR_COMPLETE)return stopped(t);
 if(t->phase==M16_FIR_VALIDATE||t->phase==M16_FIR_WAIT_SLOT){
  rc=m16_projection_runtime_unclaim(r,t);if(rc!=1)return fail(t,M16_FIR_OWNERSHIP);
  return stopped(t);
 }
 if(t->phase==M16_FIR_FAILED){
  if(m16_projection_runtime_client_released(r,t))return stopped(t);
  if(m16_projection_runtime_prepare_available(r,t)){
   rc=m16_projection_runtime_unclaim(r,t);return rc==1?stopped(t):t->error;
  }
  return t->error;
 }
 if(t->phase==M16_FIR_PACK||t->phase==M16_FIR_SUBMIT){
  rc=m16_projection_runtime_unclaim(r,t);return rc==1?stopped(t):fail(t,M16_FIR_OWNERSHIP);
 }
 if(t->phase==M16_FIR_CHECK||t->phase==M16_FIR_COPY){
  rc=m16_projection_runtime_retire_for(r,t);return rc?fail(t,rc):stopped(t);
 }
 t->cancel_requested=1;
 rc=m16_projection_runtime_cancel_for(r,t);
 if(rc<0)return fail(t,rc);
 return rc==1?stopped(t):0;
}
static int provider_request(void *p,const void *c,const m16_fir_request *q){return m16_fir_task_request(p,c,q);}
static int provider_poll(void *p,const void *c){return m16_fir_task_poll(p,c);}
static int provider_cancel(void *p,const void *c){return m16_fir_task_cancel(p,c);}
static int provider_owns(const void *p,const void *c){return m16_fir_task_owns(p,c);}
int m16_fir_task_make_provider(m16_fir_task *t,m16_fir_provider *p){
 if(!valid(t)||!p||!apart(t,sizeof(*t),p,sizeof(*p))||
  !apart(t->runtime,sizeof(*t->runtime),p,sizeof(*p))||!apart(t->runtime->arena,(uintptr_t)t->runtime->words*8u,p,sizeof(*p)))return M16_FIR_INVALID;
 *p=(m16_fir_provider){t,provider_request,provider_poll,provider_cancel,provider_owns};return 0;
}
