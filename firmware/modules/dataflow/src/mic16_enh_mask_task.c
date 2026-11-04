#include "mic16_enh_mask_task.h"
#include "mic16_enh_weights.h"
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d454d31u
static int valid(const m16_enh_mask_task *t){return t && !((uintptr_t)t%_Alignof(m16_enh_mask_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
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
static int fail(m16_enh_mask_task *t,int rc){t->error=rc;t->phase=M16_EM_FAILED;return rc;}
int m16_enh_mask_task_init(m16_enh_mask_task *t,m16_enh_network_workspace *w){
 if(!t || !w || (uintptr_t)t%_Alignof(m16_enh_mask_task) || (uintptr_t)w%_Alignof(m16_enh_network_workspace) ||
  t->magic || !apart(t,sizeof(*t),w,sizeof(*w)) || !environment())return M16_EM_INVALID;
 *t=(m16_enh_mask_task){0};t->magic=MAGIC;t->work=w;return 0;
}
int m16_enh_mask_task_releasable(const m16_enh_mask_task *t){
 return valid(t) && (t->phase==M16_EM_IDLE || t->phase==M16_EM_COMPLETE || t->phase==M16_EM_STOPPED || t->phase==M16_EM_FAILED);
}
int m16_enh_mask_task_begin(m16_enh_mask_task *t,const float *x,unsigned xn,float *out,unsigned on){
 const void *p[4];uintptr_t n[4];unsigned i,j;
 if(!valid(t) || !x || !out || (uintptr_t)x%4u || (uintptr_t)out%4u || xn<67u*64u*10u || on<67u*64u)return M16_EM_INVALID;
 if(!m16_enh_mask_task_releasable(t))return M16_EM_BUSY;
 if(!environment())return M16_EM_FLOAT_ENV;
 p[0]=t;p[1]=t->work;p[2]=x;p[3]=out;n[0]=sizeof(*t);n[1]=sizeof(*t->work);n[2]=67u*64u*10u*4u;n[3]=67u*64u*4u;
 for(i=0;i<4;i++)for(j=i+1;j<4;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_EM_INVALID;
 t->features=x;t->mask=out;t->phase=M16_EM_VALIDATE;t->cursor=t->layer=t->active=t->last_items=t->max_items=0;t->steps=0;t->error=0;return 0;
}
static int step(m16_enh_mask_task *t){
 unsigned i,end;m16_enh_network_workspace *w=t->work;
 if(t->phase==M16_EM_COMPLETE)return 0;
 if(t->phase==M16_EM_STOPPED)return 2;
 if(t->phase==M16_EM_FAILED)return t->error;
 if(t->phase==M16_EM_IDLE)return M16_EM_INVALID;
 if(!environment())return fail(t,M16_EM_FLOAT_ENV);
 if(t->phase==M16_EM_VALIDATE){
  end=t->cursor+64u;if(end>67u*64u*10u)end=67u*64u*10u;
  for(i=t->cursor;i<end;i++){float v=t->features[i];if(!(v>=-65504.f && v<=65504.f))return fail(t,M16_EM_INPUT);t->last_items++;}
  t->cursor=end;if(end==67u*64u*10u){t->cursor=0;t->phase=M16_EM_PROJECT;}return 1;
 }
 if(t->phase==M16_EM_PROJECT){
  end=t->cursor+8u;if(end>67u*64u*24u)end=67u*64u*24u;
  for(i=t->cursor;i<end;i++){
   unsigned row=i/24u,o=i%24u,c;float sum=m16_enh_project_bias[o];
   for(c=0;c<10u;c++){
    float v=t->features[row*10u+c];if(!(v>=-65504.f && v<=65504.f))return fail(t,M16_EM_INPUT);
    sum+=m16_enh_project_weight[o*10u+c]*m16_enh_half_roundtrip(v);t->last_items++;
   }
   w->h[0][i]=sum>0.f?sum:0.f;
  }
  t->cursor=end;if(end==67u*64u*24u){t->cursor=t->active=t->layer=0;t->phase=M16_EM_DEPTH;}return 1;
 }
 if(t->phase==M16_EM_DEPTH){
  const float *dw=m16_enh_depth_weight+t->layer*24u*9u,*db=m16_enh_depth_bias+t->layer*24u;int dilation=1<<(int)t->layer;
  end=t->cursor+8u;if(end>67u*64u*24u)end=67u*64u*24u;
  for(i=t->cursor;i<end;i++){
   int time=(int)(i/(64u*24u)),band=(int)((i/24u)%64u),c=(int)(i%24u),kt,kf;float sum=db[c];
   for(kt=0;kt<3;kt++)for(kf=0;kf<3;kf++){
    int tt=time+(kt-2)*dilation,ff=band+kf-1;
    if(tt>=0 && ff>=0 && ff<64){sum+=dw[c*9+kt*3+kf]*w->h[t->active][(tt*64+ff)*24+c];t->last_items++;}
   }
   w->depth[i]=sum>0.f?sum:0.f;
  }
  t->cursor=end;if(end==67u*64u*24u){t->cursor=0;t->phase=M16_EM_POINT;}return 1;
 }
 if(t->phase==M16_EM_POINT){
  const float *pw=m16_enh_point_weight+t->layer*24u*24u,*pb=m16_enh_point_bias+t->layer*24u;
  end=t->cursor+8u;if(end>67u*64u*24u)end=67u*64u*24u;
  for(i=t->cursor;i<end;i++){
   unsigned row=i/24u,o=i%24u,c;float sum=pb[o];
   for(c=0;c<24u;c++){sum+=pw[o*24u+c]*w->depth[row*24u+c];t->last_items++;}
   sum+=w->h[t->active][i];w->h[1u-t->active][i]=sum>0.f?sum:0.f;
  }
  t->cursor=end;if(end==67u*64u*24u){
   t->active=1u-t->active;t->cursor=0;t->phase=++t->layer==3u?M16_EM_OUTPUT:M16_EM_DEPTH;
  }return 1;
 }
 if(t->phase==M16_EM_OUTPUT){
  end=t->cursor+8u;if(end>67u*64u)end=67u*64u;
  for(i=t->cursor;i<end;i++){
   unsigned c;float sum=m16_enh_output_bias[0],base;
   for(c=0;c<24u;c++){sum+=m16_enh_output_weight[c]*w->h[t->active][i*24u+c];t->last_items++;}
   base=1.f+.5f*tanhf(sum);t->mask[i]=1.f+.4f*(base-1.f);
  }
  t->cursor=end;if(end==67u*64u){t->cursor=0;t->phase=M16_EM_CHECK;}return 1;
 }
 if(t->phase==M16_EM_CHECK){
  end=t->cursor+64u;if(end>67u*64u)end=67u*64u;
  for(i=t->cursor;i<end;i++){float v=t->mask[i];if(!(v>=.8f-1e-6f && v<=1.2f+1e-6f))return fail(t,M16_EM_OUTPUT_ERROR);t->last_items++;}
  t->cursor=end;if(end==67u*64u){t->phase=M16_EM_COMPLETE;return 0;}return 1;
 }
 return fail(t,M16_EM_INVALID);
}
int m16_enh_mask_task_step(m16_enh_mask_task *t){
 int rc;if(!valid(t))return M16_EM_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_enh_mask_task_stop(m16_enh_mask_task *t){if(!valid(t))return M16_EM_INVALID;t->phase=M16_EM_STOPPED;return 1;}
