#include "mic16_candidate_pool_task.h"
#include "mic16_spatial_tables.h"
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d43504cu
#define PI 3.14159265358979323846264338327950288
static int valid(const m16_candidate_pool_task *t){return t && !((uintptr_t)t%_Alignof(m16_candidate_pool_task)) && t->magic==MAGIC;}
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
static int fail(m16_candidate_pool_task *t,int rc){t->error=rc;t->phase=M16_CP_FAILED;return rc;}
static unsigned bit(const unsigned char *a,unsigned k){return (a[k/8u]>>(k%8u))&1u;}
/* 沿用冻结NumPy式128项叶子归约，480项的最大递归深度仅3层。 */
static double weight_sum(const double *a,int n){
 int i,j;double s=-0.;
 if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
 if(n<=128){
  double lanes[8];for(j=0;j<8;j++)lanes[j]=a[j];
  for(i=8;i<n-n%8;i+=8)for(j=0;j<8;j++)lanes[j]+=a[i+j];
  s=((lanes[0]+lanes[1])+(lanes[2]+lanes[3]))+((lanes[4]+lanes[5])+(lanes[6]+lanes[7]));
  for(;i<n;i++)s+=a[i];
  return s;
 }
 i=(n/2)/8*8;return weight_sum(a,i)+weight_sum(a+i,n-i);
}
static int better(const mic16_cluster *a,const mic16_cluster *b){
 if(a->frames!=b->frames)return a->frames>b->frames;
 if(a->bands!=b->bands)return a->bands>b->bands;
 if(a->votes!=b->votes)return a->votes>b->votes;
 return a->weight_sum>b->weight_sum;
}
int m16_candidate_pool_init(m16_candidate_pool_task *t,mic16_spatial_workspace *w){
 if(!t || !w || (uintptr_t)t%_Alignof(m16_candidate_pool_task) || (uintptr_t)w%_Alignof(mic16_spatial_workspace) ||
  t->magic || !apart(t,sizeof(*t),w,sizeof(*w)) || !environment())return M16_CP_INVALID;
 *t=(m16_candidate_pool_task){0};t->magic=MAGIC;t->work=w;return 0;
}
int m16_candidate_pool_releasable(const m16_candidate_pool_task *t){
 return valid(t) && (t->phase==M16_CP_IDLE || t->phase==M16_CP_COMPLETE || t->phase==M16_CP_STOPPED || t->phase==M16_CP_FAILED);
}
int m16_candidate_pool_begin(m16_candidate_pool_task *t,const float *maps,unsigned mn,const double *levels,unsigned ln,
 const double *floor,double *votes,unsigned vn,unsigned short *best_indices,mic16_pool *out){
 const void *p[8];uintptr_t n[8];unsigned i,j;
 if(!valid(t) || !maps || !levels || !floor || !votes || !best_indices || !out || mn<M16_SP_MAP_FLOATS || ln<480u || vn<480u*9u ||
  (uintptr_t)maps%4u || (uintptr_t)levels%8u || (uintptr_t)floor%8u || (uintptr_t)votes%8u || (uintptr_t)best_indices%2u ||
  (uintptr_t)out%_Alignof(mic16_pool))return M16_CP_INVALID;
 if(!m16_candidate_pool_releasable(t))return M16_CP_BUSY;
 if(!environment())return M16_CP_FLOAT_ENV;
 p[0]=t;p[1]=t->work;p[2]=maps;p[3]=levels;p[4]=floor;p[5]=votes;p[6]=best_indices;p[7]=out;
 n[0]=sizeof(*t);n[1]=sizeof(*t->work);n[2]=M16_SP_MAP_FLOATS*4u;n[3]=480u*8u;n[4]=24u*8u;n[5]=480u*9u*8u;n[6]=480u*2u;n[7]=sizeof(*out);
 for(i=0;i<8;i++)for(j=i+1;j<8;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_CP_INVALID;
 t->maps=maps;t->levels=levels;t->floor=floor;t->votes=votes;t->best_indices=best_indices;t->output=out;
 t->phase=M16_CP_VALIDATE_MAP;t->cursor=t->row=t->seed_count=t->seed=t->last_items=t->max_items=0;t->error=0;t->steps=0;return 0;
}
static int step(m16_candidate_pool_task *t){
 unsigned i,end;mic16_spatial_workspace *w=t->work;
 if(t->phase==M16_CP_COMPLETE)return 0;
 if(t->phase==M16_CP_STOPPED)return 2;
 if(t->phase==M16_CP_FAILED)return t->error;
 if(t->phase==M16_CP_IDLE)return M16_CP_INVALID;
 if(!environment())return fail(t,M16_CP_FLOAT_ENV);
 if(t->phase==M16_CP_VALIDATE_MAP){
  end=t->cursor+64u;if(end>M16_SP_MAP_FLOATS)end=M16_SP_MAP_FLOATS;
  for(i=t->cursor;i<end;i++){float x=t->maps[i];if(!(x>=-2.f && x<=2.f))return fail(t,M16_CP_INPUT);t->last_items++;}
  t->cursor=end;if(end==M16_SP_MAP_FLOATS){t->cursor=0;t->phase=M16_CP_VALIDATE_LEVEL;}return 1;
 }
 if(t->phase==M16_CP_VALIDATE_LEVEL || t->phase==M16_CP_VALIDATE_FLOOR){
  unsigned n=t->phase==M16_CP_VALIDATE_LEVEL?480u:24u;const double *p=t->phase==M16_CP_VALIDATE_LEVEL?t->levels:t->floor;
  end=t->cursor+64u;if(end>n)end=n;
  for(i=t->cursor;i<end;i++){if(!(p[i]>=-1000. && p[i]<=1000.))return fail(t,M16_CP_INPUT);t->last_items++;}
  t->cursor=end;if(end==n){t->cursor=0;t->phase=t->phase==M16_CP_VALIDATE_LEVEL?M16_CP_VALIDATE_FLOOR:M16_CP_BEST;t->best=0;t->row=0;}return 1;
 }
 if(t->phase==M16_CP_BEST || t->phase==M16_CP_SECOND){
  const float *p=t->maps+t->row*325u;end=t->cursor+64u;if(end>325u)end=325u;
  for(i=t->cursor;i<end;i++){
   if(!(p[i]>=-2.f && p[i]<=2.f))return fail(t,M16_CP_INPUT);
   if(t->phase==M16_CP_BEST){if(i>0 && p[i]>p[t->best])t->best=i;}
   else if(bit(m16_second15[t->best],i) && p[i]>t->second)t->second=p[i];
   t->last_items++;
  }
  t->cursor=end;if(end==325u){t->cursor=0;if(t->phase==M16_CP_BEST){t->second=-3.f;t->phase=M16_CP_SECOND;}else t->phase=M16_CP_VOTE;}return 1;
 }
 if(t->phase==M16_CP_VOTE){
  unsigned frame=t->row/24u,z=t->row%24u,start=z*34u,last=start+34u;double center,delta,*v=t->votes+t->row*9u;
  if(last>807u)last=807u;
  center=(32.+(double)(start+last-1u)/2.)*(39062.5/4096.);
  delta=(double)t->maps[t->row*325u+t->best]-(double)t->second;
  if(!(t->levels[t->row]>=-1000. && t->levels[t->row]<=1000. && t->floor[z]>=-1000. && t->floor[z]<=1000.))return fail(t,M16_CP_INPUT);
  v[0]=frame;v[1]=z;v[2]=center<1500.?0.:(center<3500.?1.:2.);v[3]=center;
  v[4]=-60.+5.*(t->best%25u);v[5]=-30.+5.*(t->best/25u);v[6]=t->levels[t->row]-t->floor[z];
  v[7]=(double)t->maps[t->row*325u+t->best];v[8]=delta>0.?delta:0.;t->best_indices[t->row]=(unsigned short)t->best;t->last_items=9;
  t->best=t->cursor=0;if(++t->row==480u)t->phase=M16_CP_CLEAR;else t->phase=M16_CP_BEST;return 1;
 }
 if(t->phase==M16_CP_CLEAR){
  end=t->cursor+64u;if(end>325u)end=325u;
  for(i=t->cursor;i<end;i++){t->seen[i]=0;t->last_items++;}
  t->cursor=end;if(end==325u){t->cursor=0;t->phase=M16_CP_SEEDS;}return 1;
 }
 if(t->phase==M16_CP_SEEDS){
  end=t->cursor+7u;if(end>480u)end=480u;
  for(i=t->cursor;i<end;i++){
   const double *a=t->votes+i*9u;unsigned j;int ai,ei,g;
   for(j=0;j<9u;j++){if(!finite64(a[j]))return fail(t,M16_CP_INPUT);t->last_items++;}
   if(a[0]!=(double)(i/24u) || a[1]!=(double)(i%24u) || a[2]<0. || a[2]>2. || a[2]!=(double)(int)a[2] ||
    a[4]<-60. || a[4]>60. || a[5]<-30. || a[5]>30. || a[6]<-2000. || a[6]>2000. ||
    a[7]<-2. || a[7]>2. || a[8]<0. || a[8]>4.)return fail(t,M16_CP_INPUT);
   ai=(int)((a[4]+60.)/5.);ei=(int)((a[5]+30.)/5.);
   if(a[4]!=-60.+5.*ai || a[5]!=-30.+5.*ei)return fail(t,M16_CP_INPUT);
   g=ei*25+ai;if(!t->seen[g]){t->seeds[t->seed_count++]=(unsigned)g;t->seen[g]=1;}
  }
  t->cursor=end;if(end==480u){t->cursor=t->weight_count=t->frame_bits=t->band_bits=0;t->phase=M16_CP_WEIGHTS;}return 1;
 }
 if(t->phase==M16_CP_WEIGHTS || t->phase==M16_CP_CENTER){
  unsigned seed=t->seeds[t->seed];end=t->cursor+16u;if(end>480u)end=480u;
  for(i=t->cursor;i<end;i++){
   const double *a=t->votes+i*9u;int g;double weight;t->last_items++;
   if(!(a[4]>=-60. && a[4]<=60. && a[5]>=-30. && a[5]<=30. && a[2]>=0. && a[2]<=2.) ||
    a[2]!=(double)(int)a[2] || !finite64(a[6]) || !finite64(a[7]) || !finite64(a[8]))return fail(t,M16_CP_INPUT);
   g=(int)((a[5]+30.)/5.)*25+(int)((a[4]+60.)/5.);
   if(g<0 || g>=325)return fail(t,M16_CP_INPUT);
   if(!bit(m16_near10[seed],(unsigned)g))continue;
   if(t->phase==M16_CP_WEIGHTS){
    w->weights[t->weight_count++]=(a[7]>0.?a[7]:0.)*(a[8]>0.?a[8]:0.)*(a[6]>0.?a[6]:0.);
    t->frame_bits|=1u<<(i/24u);t->band_bits|=1u<<(unsigned)a[2];
   }else{
    weight=t->weight_sum<=1e-12?1.:w->weights[t->weight_index];t->weight_index++;
    t->center[0]+=(double)m16_grid_unit[g][0]*weight;t->center[1]+=(double)m16_grid_unit[g][1]*weight;t->center[2]+=(double)m16_grid_unit[g][2]*weight;
   }
  }
  t->cursor=end;if(end==480u){t->cursor=0;t->phase=t->phase==M16_CP_WEIGHTS?M16_CP_SUM:M16_CP_CLUSTER;}return 1;
 }
 if(t->phase==M16_CP_SUM){
  t->weight_sum=weight_sum(w->weights,(int)t->weight_count);t->last_items=t->weight_count;
  t->center[0]=t->center[1]=t->center[2]=0.;t->weight_index=0;t->phase=M16_CP_CENTER;return 1;
 }
 if(t->phase==M16_CP_CLUSTER){
  mic16_cluster *c=w->clusters+t->seed;double norm=sqrt((t->center[0]*t->center[0]+t->center[1]*t->center[1])+t->center[2]*t->center[2]);
  double y;unsigned j;if(norm<1e-12)norm=1e-12;
  for(j=0;j<3u;j++)t->center[j]/=norm;
  y=t->center[1];y=y< -1.?-1.:(y>1.?1.:y);
  c->direction[0]=atan2(t->center[0],t->center[2])*(180./PI);c->direction[1]=asin(y)*(180./PI);
  c->weight_sum=t->weight_sum;c->seed_grid=t->seeds[t->seed];c->votes=t->weight_count;c->frames=c->bands=0;
  for(j=0;j<20u;j++)c->frames+=(t->frame_bits>>j)&1u;
  for(j=0;j<3u;j++)c->bands+=(t->band_bits>>j)&1u;
  t->last_items=23;
  if(++t->seed==t->seed_count){t->sort_index=1;t->sort_loaded=0;t->phase=M16_CP_SORT;}
  else{t->cursor=t->weight_count=t->frame_bits=t->band_bits=0;t->phase=M16_CP_WEIGHTS;}return 1;
 }
 if(t->phase==M16_CP_SORT){
  unsigned moves=0;
  if(t->sort_index>=t->seed_count){t->selected_index=t->selected_peer=0;t->output->count=0;t->phase=M16_CP_SELECT;return 1;}
  if(!t->sort_loaded){t->sorting=w->clusters[t->sort_index];t->sort_at=t->sort_index;t->sort_loaded=1;}
  while(moves<16u && t->sort_at>0 && better(&t->sorting,&w->clusters[t->sort_at-1u])){
   w->clusters[t->sort_at]=w->clusters[t->sort_at-1u];t->sort_at--;moves++;
  }
  t->last_items=moves;
  if(t->sort_at==0 || !better(&t->sorting,&w->clusters[t->sort_at-1u])){w->clusters[t->sort_at]=t->sorting;t->sort_loaded=0;t->sort_index++;}return 1;
 }
 if(t->phase==M16_CP_SELECT){
  mic16_cluster *c;unsigned k=t->output->count,j;
  if(t->selected_index==t->seed_count || k==16u){t->phase=M16_CP_COMPLETE;return 0;}
  c=w->clusters+t->selected_index;end=t->selected_peer+4u;if(end>k)end=k;
  for(j=t->selected_peer;j<end;j++){
   t->last_items++;
   if(mic16_spatial_angle(c->direction[0],c->direction[1],t->output->items[j].direction[0],t->output->items[j].direction[1])<10.){
    t->selected_peer=0;t->selected_index++;return 1;
   }
  }
  t->selected_peer=end;if(end==k){t->output->items[k]=*c;t->output->count=k+1u;t->selected_peer=0;t->selected_index++;}return 1;
 }
 return fail(t,M16_CP_INVALID);
}
int m16_candidate_pool_step(m16_candidate_pool_task *t){
 int rc;if(!valid(t))return M16_CP_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_candidate_pool_stop(m16_candidate_pool_task *t){if(!valid(t))return M16_CP_INVALID;t->phase=M16_CP_STOPPED;return 1;}
