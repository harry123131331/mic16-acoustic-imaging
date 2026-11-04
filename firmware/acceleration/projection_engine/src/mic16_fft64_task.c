#include "mic16_fft64_task.h"
#include <string.h>
#define MAGIC 0x4d463634u
#define ABI M16_PROJECTION_EXPECTED_ABI
static int valid(const m16_fft64_task *t){return t&&t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static uint64_t bits(double v){uint64_t x;memcpy(&x,&v,8);return x;}
static double number(uint64_t v){double x;memcpy(&x,&v,8);return x;}
static int finite_word(uint64_t x){return (x&UINT64_C(0x7fffffffffffffff))<UINT64_C(0x7ff0000000000000);}
static int fail(m16_fft64_task *t,int rc){t->error=rc;t->phase=M16_F64_FAILED;return rc;}
int m16_fft64_init(m16_fft64_task *t,m16_projection_runtime *r,unsigned abi){
 if(!t||!r||t->magic||(uintptr_t)t%_Alignof(m16_fft64_task)||!m16_projection_runtime_available(r)||
    !apart(t,sizeof(*t),r,sizeof(*r))||!apart(t,sizeof(*t),r->arena,(uintptr_t)r->words*8u))return M16_F64_INVALID;
 if(abi!=ABI||M16_PROJECTION_BUILD_ABI!=ABI||(M16_PROJECTION_VERSION!=5u&&M16_PROJECTION_VERSION!=6u))return M16_F64_ABI;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->runtime=r;return 0;
}
int m16_fft64_register_twiddles(m16_fft64_task *t,const double *wr,const double *wi,unsigned n,unsigned table){
 unsigned index;int rc;
 if(!valid(t)||!wr||!wi||t->client||t->pending_client||!m16_projection_runtime_available(t->runtime)||
    (n!=256u&&n!=512u&&n!=2048u&&n!=4096u)||(table!=512u&&table!=4096u)||table<n||
    (uintptr_t)wr%8u||(uintptr_t)wi%8u||!apart(t,sizeof(*t),wr,(uintptr_t)table*4u)||
    !apart(t,sizeof(*t),wi,(uintptr_t)table*4u))return M16_F64_INVALID;
 for(index=0;index<t->guard_count;index++){
  const m16_fft64_range_guard *g=&t->guards[index];
  if(g->twiddle_real==wr&&g->twiddle_imaginary==wi&&g->n==n&&g->table_length==table)break;
 }
 if(index==M16_F64_MAX_GUARDS)return M16_F64_INVALID;
 rc=m16_fft64_range_prepare(&t->preparing_guard,wr,wi,n,table,t->guard_scratch);
 if(rc)return rc==2?2:M16_F64_INVALID;
 t->guards[index]=t->preparing_guard;
 if(index==t->guard_count)t->guard_count++;
 return 0;
}
static int use_original_cpu(m16_fft64_task *t,unsigned reason){
 if(m16_projection_runtime_unclaim(t->runtime,t)!=1)return fail(t,M16_F64_OWNERSHIP);
 t->fallback_reason=reason;
 if(reason==1u)t->range_fallbacks++;else t->coefficient_fallbacks++;
 t->phase=M16_F64_CPU_FALLBACK;return 1;
}
int m16_fft64_owns(const m16_fft64_task *t,const void *client){return valid(t)&&client&&(t->client==client||t->pending_client==client);}
int m16_fft64_request(m16_fft64_task *t,const void *client,double *re,double *im,unsigned n,
 const double *wr,const double *wi,unsigned table,unsigned inverse){
 const void *p[7];uintptr_t z[7];unsigned i,j,end,guard_index;m16_projection_runtime *r;
 if(!valid(t)||!client||!re||!im||!wr||!wi||(n!=256u&&n!=512u&&n!=2048u&&n!=4096u)||
    (table!=512u&&table!=4096u)||table<n||inverse>1u||(uintptr_t)re%8u||(uintptr_t)im%8u||
    (uintptr_t)wr%8u||(uintptr_t)wi%8u)return M16_F64_INVALID;
 r=t->runtime;
 if(r->phase==M16_PR_QUARANTINE)return r->report.error;
 if(t->client||(t->pending_client&&t->pending_client!=client)||!m16_projection_runtime_schedulable(r))return 1;
 p[0]=t;z[0]=sizeof(*t);p[1]=r;z[1]=sizeof(*r);p[2]=r->arena;z[2]=(uintptr_t)r->words*8u;
 p[3]=re;z[3]=(uintptr_t)n*8u;p[4]=im;z[4]=(uintptr_t)n*8u;p[5]=wr;z[5]=(uintptr_t)table*4u;p[6]=wi;z[6]=(uintptr_t)table*4u;
 for(i=0;i<7;i++)for(j=i+1;j<7;j++)if(!apart(p[i],z[i],p[j],z[j]))return M16_F64_INVALID;
 t->input_at=24u;t->output_at=t->input_at+2u*n;t->twiddle_at=t->output_at+2u*n;end=t->twiddle_at+table;
 if(end>r->words-8u||r->tag_high==UINT64_MAX)return M16_F64_INVALID;
 for(guard_index=0;guard_index<t->guard_count;guard_index++){
  const m16_fft64_range_guard *g=&t->guards[guard_index];
  if(g->twiddle_real==wr&&g->twiddle_imaginary==wi&&g->n==n&&g->table_length==table)break;
 }
 if(guard_index==t->guard_count){
  t->missing_table_fallbacks++;
  if(t->pending_client==client){(void)m16_projection_runtime_unclaim(r,t);t->pending_client=0;}
  return 2;
 }
 int claim=m16_projection_runtime_claim(r,t,2u);
 if(claim==1){t->pending_client=client;return 1;}
 if(claim<0){(void)m16_projection_runtime_unclaim(r,t);t->pending_client=0;return claim;}
 t->pending_client=0;
 t->tag=r->tag_high+1u;t->request_tag=r->tag_high;t->client=client;t->real=re;t->imaginary=im;
 t->twiddle_real=wr;t->twiddle_imaginary=wi;t->n=n;t->table=table;t->inverse=inverse;
 t->cursor=t->cancel_requested=t->last_items=0;t->error=0;t->guard_index=guard_index;t->fallback_reason=0;
 m16_fft64_range_begin(&t->input_range);t->phase=M16_F64_INPUT;return 0;
}
int m16_fft64_service(m16_fft64_task *t){
 unsigned i,end;int rc;m16_projection_runtime *r;
 if(!valid(t))return M16_F64_INVALID;
 t->steps++;t->last_items=0;r=t->runtime;
 if(t->phase==M16_F64_IDLE||t->phase==M16_F64_COMPLETE||t->phase==M16_F64_STOPPED||t->phase==M16_F64_CPU_FALLBACK)return 0;
 if(t->phase==M16_F64_FAILED)return t->error;
 if(t->phase<=M16_F64_SUBMIT&&(!m16_projection_runtime_prepare_available(r,t)||r->tag_high!=t->request_tag))return fail(t,M16_F64_OWNERSHIP);
 end=t->cursor+32u;
 if(t->phase==M16_F64_INPUT){
  if(end>t->n)end=t->n;
  for(i=t->cursor;i<end;i++){
   uint64_t a=bits(t->real[i]),b=bits(t->imaginary[i]);if(!finite_word(a)||!finite_word(b))return fail(t,M16_F64_INPUT_ERROR);
   m16_fft64_range_observe(&t->input_range,a);m16_fft64_range_observe(&t->input_range,b);
   r->arena[t->input_at+2u*i]=a;r->arena[t->input_at+2u*i+1u]=b;t->last_items+=2u;
  }
  t->cursor=end;if(end==t->n){
   if(m16_fft64_range_accepts(&t->guards[t->guard_index],&t->input_range,t->inverse)!=0)return use_original_cpu(t,1u);
   t->cursor=0;t->phase=M16_F64_TWIDDLE;
  }return 1;
 }
 if(t->phase==M16_F64_TWIDDLE){
  if(end>t->table/2u)end=t->table/2u;
  for(i=t->cursor;i<end;i++){
   uint64_t a=bits(t->twiddle_real[i]),b=bits(t->twiddle_imaginary[i]);
   if(m16_fft64_range_coefficient_accepts(&t->guards[t->guard_index],i,a,b)!=0)return use_original_cpu(t,2u);
   r->arena[t->twiddle_at+2u*i]=a;r->arena[t->twiddle_at+2u*i+1u]=b;t->last_items+=2u;
  }
  t->cursor=end;if(end==t->table/2u){t->cursor=0;t->phase=M16_F64_SUBMIT;}return 1;
 }
 if(t->phase==M16_F64_SUBMIT){
  uint64_t *d=r->arena+8;
  d[0]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|(UINT64_C(4)<<48);d[1]=t->tag;
  d[2]=t->input_at|((uint64_t)t->twiddle_at<<32);d[3]=t->output_at|(UINT64_C(16)<<32);
  d[4]=t->n|((uint64_t)t->table<<16)|((uint64_t)(t->inverse+1u)<<32);d[5]=t->n*2u;
  d[6]=t->n*2u|((uint64_t)t->table<<32);d[7]=0;
  rc=m16_projection_runtime_submit_for(r,t,8u,1u);if(rc)return fail(t,rc);
  t->transactions++;t->phase=M16_F64_DEVICE;return 1;
 }
 if(t->phase==M16_F64_DEVICE){
  rc=m16_projection_runtime_poll_for(r,t);if(rc<0)return fail(t,rc);if(rc)return 1;
  t->cursor=0;t->phase=M16_F64_CHECK;return 1;
 }
 if(t->phase==M16_F64_CHECK){
  if(t->cancel_requested){
   rc=m16_projection_runtime_retire_for(r,t);if(rc)return fail(t,rc);
   t->phase=M16_F64_STOPPED;t->client=0;return 0;
  }
  if(end>2u*t->n)end=2u*t->n;
  for(i=t->cursor;i<end;i++){if(!finite_word(r->arena[t->output_at+i]))return fail(t,M16_F64_OUTPUT_ERROR);t->last_items++;}
  t->cursor=end;if(end==2u*t->n){t->cursor=0;t->phase=M16_F64_COPY;}return 1;
 }
 if(t->phase==M16_F64_COPY){
  if(t->cancel_requested){
   rc=m16_projection_runtime_retire_for(r,t);if(rc)return fail(t,rc);t->phase=M16_F64_STOPPED;t->client=0;return 0;
  }
  if(end>t->n)end=t->n;
  for(i=t->cursor;i<end;i++){
   t->real[i]=number(r->arena[t->output_at+2u*i]);t->imaginary[i]=number(r->arena[t->output_at+2u*i+1u]);t->last_items+=2u;
  }
  t->cursor=end;if(end==t->n){rc=m16_projection_runtime_retire_for(r,t);if(rc)return fail(t,rc);t->phase=M16_F64_COMPLETE;return 0;}return 1;
 }
 return fail(t,M16_F64_INVALID);
}
int m16_fft64_poll(m16_fft64_task *t,const void *client){
 int rc;if(!m16_fft64_owns(t,client))return M16_F64_INVALID;
 if(t->pending_client==client)return 1;
 rc=m16_fft64_service(t);if(rc<0)return rc;
 if(t->phase==M16_F64_CPU_FALLBACK){t->client=0;t->phase=M16_F64_IDLE;return 2;}
 if(t->phase==M16_F64_COMPLETE){t->client=0;t->phase=M16_F64_IDLE;return 0;}return 1;
}
int m16_fft64_cancel(m16_fft64_task *t,const void *client){
 int rc;
 if(!valid(t)||!client)return M16_F64_INVALID;
 if(t->pending_client==client){
  (void)m16_projection_runtime_unclaim(t->runtime,t);t->pending_client=0;return 1;
 }
 if(t->client!=client)return 1;
 if(t->phase==M16_F64_CPU_FALLBACK){t->client=0;t->phase=M16_F64_STOPPED;return 1;}
 if(t->phase==M16_F64_FAILED){
  // 只允许尚未交设备的输入错误清客户端；超时/结果错误仍隔离，禁止复用。
  if(m16_projection_runtime_prepare_available(t->runtime,t)){
   (void)m16_projection_runtime_unclaim(t->runtime,t);t->client=0;t->phase=M16_F64_STOPPED;return 1;}return t->error;
 }
 if(t->phase<=M16_F64_SUBMIT||t->phase==M16_F64_COMPLETE){
  (void)m16_projection_runtime_unclaim(t->runtime,t);t->client=0;t->phase=M16_F64_STOPPED;return 1;}
 t->cancel_requested=1;rc=m16_projection_runtime_cancel_for(t->runtime,t);
 if(rc<0)return fail(t,rc);
 if(rc==1){t->client=0;t->phase=M16_F64_STOPPED;return 1;}
 return 0;
}
