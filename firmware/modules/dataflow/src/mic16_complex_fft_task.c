#include "mic16_complex_fft_task.h"
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d434631u
static int valid(const m16_complex_fft_task *t){return t && !((uintptr_t)t%_Alignof(m16_complex_fft_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
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
static int fail(m16_complex_fft_task *t,int rc){t->error=rc;t->phase=M16_CF_FAILED;return rc;}
int m16_complex_fft_init(m16_complex_fft_task *t,double *r,double *im,unsigned capacity,const double *wr,const double *wi,unsigned table){
 const void *p[5];uintptr_t n[5];unsigned i,j;
 if(!t || !r || !im || !wr || !wi || (uintptr_t)t%_Alignof(m16_complex_fft_task) || t->magic ||
  (capacity!=512u && capacity!=4096u) || (table!=512u && table!=4096u) || table<capacity ||
  (uintptr_t)r%8u || (uintptr_t)im%8u || (uintptr_t)wr%8u || (uintptr_t)wi%8u || !environment())return M16_CF_INVALID;
 p[0]=t;p[1]=r;p[2]=im;p[3]=wr;p[4]=wi;n[0]=sizeof(*t);n[1]=n[2]=(uintptr_t)capacity*8u;n[3]=n[4]=(uintptr_t)(table/2u)*8u;
 for(i=0;i<5;i++)for(j=i+1;j<5;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_CF_INVALID;
 *t=(m16_complex_fft_task){0};t->magic=MAGIC;t->real=r;t->imaginary=im;t->twiddle_real=wr;t->twiddle_imaginary=wi;t->capacity=capacity;t->table_length=table;return 0;
}
int m16_complex_fft_releasable(const m16_complex_fft_task *t){
 return valid(t) && (t->phase==M16_CF_IDLE || t->phase==M16_CF_COMPLETE || t->phase==M16_CF_STOPPED || t->phase==M16_CF_FAILED) &&
  (!t->provider || !t->provider->owns(t->provider->context,t));
}
int m16_complex_fft_bind(m16_complex_fft_task *t,const m16_fft_provider *p){
 if(!valid(t)||!p||!p->context||!p->request||!p->poll||!p->cancel||!p->owns||t->provider||!m16_complex_fft_releasable(t))return M16_CF_INVALID;
 t->provider=p;return 0;
}
int m16_complex_fft_begin(m16_complex_fft_task *t,unsigned n,unsigned inverse){
 if(!valid(t) || (n!=512u && n!=4096u) || n>t->capacity || n>t->table_length || inverse>1u)return M16_CF_INVALID;
 if(!m16_complex_fft_releasable(t))return M16_CF_BUSY;
 if(!environment())return M16_CF_FLOAT_ENV;
 t->n=n;t->inverse=inverse;t->phase=M16_CF_VALIDATE;t->cursor=t->reverse=t->last_items=t->max_items=0;t->error=0;t->steps=0;return 0;
}
static int step(m16_complex_fft_task *t){
 unsigned i,end;double *r=t->real,*im=t->imaginary;
 if(t->phase==M16_CF_COMPLETE)return 0;
 if(t->phase==M16_CF_STOPPED)return 2;
 if(t->phase==M16_CF_FAILED)return t->error;
 if(t->phase==M16_CF_IDLE)return M16_CF_INVALID;
 if(!environment())return fail(t,M16_CF_FLOAT_ENV);
 if(t->phase==M16_CF_DISPATCH){
  int rc=t->provider->request(t->provider->context,t,r,im,t->n,t->twiddle_real,t->twiddle_imaginary,t->table_length,t->inverse);
  if(rc<0)return fail(t,rc);
  if(rc==2){t->cursor=1;t->reverse=0;t->phase=M16_CF_REVERSE;return 1;}
  if(!rc){t->phase=M16_CF_DEVICE;}
  return 1;
 }
 if(t->phase==M16_CF_DEVICE){
  int rc=t->provider->poll(t->provider->context,t);if(rc<0)return fail(t,rc);
  if(rc==2){t->cursor=1;t->reverse=0;t->phase=M16_CF_REVERSE;return 1;}
  if(!rc){t->cursor=0;t->phase=M16_CF_CHECK;}return 1;
 }
 end=t->cursor+64u;
 if(t->phase==M16_CF_VALIDATE || t->phase==M16_CF_CHECK){
  if(end>t->n)end=t->n;
  for(i=t->cursor;i<end;i++){
   if(!finite64(r[i]) || !finite64(im[i]))return fail(t,t->phase==M16_CF_VALIDATE?M16_CF_INPUT:M16_CF_OUTPUT);
   t->last_items++;
  }
  t->cursor=end;if(end==t->n){
   if(t->phase==M16_CF_CHECK){t->phase=M16_CF_COMPLETE;return 0;}
   t->cursor=t->provider?0u:1u;t->reverse=0;t->phase=t->provider?M16_CF_DISPATCH:M16_CF_REVERSE;
  }return 1;
 }
 if(t->phase==M16_CF_REVERSE){
  if(end>t->n)end=t->n;
  for(i=t->cursor;i<end;i++){
   unsigned bit=t->n/2u,j=t->reverse;for(;j&bit;bit>>=1)j^=bit;j^=bit;t->reverse=j;
   if(i<j){double a=r[i],b=im[i];r[i]=r[j];im[i]=im[j];r[j]=a;im[j]=b;}t->last_items++;
  }
  t->cursor=end;if(end==t->n){t->cursor=0;t->length=2;t->phase=M16_CF_BUTTERFLIES;}return 1;
 }
 if(t->phase==M16_CF_BUTTERFLIES){
  /* length为2的幂；只改整数寻址，不改变FP64蝶形顺序。 */
  unsigned half=t->length/2u,count=t->n/2u,mask=half-1u,stride=t->table_length/t->length;if(end>count)end=count;
  for(i=t->cursor;i<end;i++){
   unsigned k=i&mask,a=(i<<1)-k,b=a+half,q=k*stride;
   double wr=t->twiddle_real[q],wi=t->inverse?-t->twiddle_imaginary[q]:t->twiddle_imaginary[q];
   double re,ii,ar,ai;
   if(!finite64(wr) || !finite64(wi))return fail(t,M16_CF_INPUT);
   re=wr*r[b]-wi*im[b];ii=wr*im[b]+wi*r[b];ar=r[a];ai=im[a];
   r[a]=ar+re;im[a]=ai+ii;r[b]=ar-re;im[b]=ai-ii;t->last_items++;
  }
  t->cursor=end;if(end==count){t->cursor=0;t->length<<=1;if(t->length>t->n)t->phase=t->inverse?M16_CF_SCALE:M16_CF_CHECK;}return 1;
 }
 if(t->phase==M16_CF_SCALE){
  if(end>t->n)end=t->n;
  for(i=t->cursor;i<end;i++){r[i]/=(double)t->n;im[i]/=(double)t->n;t->last_items++;}
  t->cursor=end;if(end==t->n){t->cursor=0;t->phase=M16_CF_CHECK;}return 1;
 }
 return fail(t,M16_CF_INVALID);
}
int m16_complex_fft_step(m16_complex_fft_task *t){
 int rc;if(!valid(t))return M16_CF_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_complex_fft_stop(m16_complex_fft_task *t){
 int rc=1;if(!valid(t))return M16_CF_INVALID;
 if(t->provider&&t->provider->owns(t->provider->context,t)){
  rc=t->provider->cancel(t->provider->context,t);if(rc<0)return fail(t,rc);
 }
 t->phase=M16_CF_STOPPED;return rc;
}
