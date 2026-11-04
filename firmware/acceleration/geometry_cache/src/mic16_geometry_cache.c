#include "mic16_geometry_cache.h"
#include "mic16_signature_dictionary_map.h"
#include <string.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x47434d31u
extern int mic16_spatial_signature(unsigned,unsigned,unsigned,float *);
_Static_assert(M16_SIGNATURE_DICTIONARY_LAGS==M16_GC_LAGS,"冻结索引维度改变");
_Static_assert(M16_SIGNATURE_DICTIONARY_WORDS==M16_GC_WORDS*2u,"冻结索引容量改变");
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
static int valid(const m16_geometry_cache *t){return t&&!((uintptr_t)t%_Alignof(m16_geometry_cache))&&t->magic==MAGIC;}
static int fail(m16_geometry_cache *t,int error){t->phase=M16_GC_FAILED;t->error=error;return error;}
int m16_geometry_cache_init(m16_geometry_cache *t,uint64_t *values,unsigned words){
 uintptr_t a=(uintptr_t)t,b=(uintptr_t)values,n=(uintptr_t)M16_GC_WORDS*8u;
 if(!t||!values||a%_Alignof(m16_geometry_cache)||(b&7u)||t->magic||words<M16_GC_WORDS||
    a>UINTPTR_MAX-sizeof(*t)||b>UINTPTR_MAX-n||(a<b+n&&b<a+sizeof(*t)))return M16_GC_INVALID;
 if(!environment())return M16_GC_ENVIRONMENT;
 memset(t,0,sizeof(*t));memset(t->representatives,0xff,sizeof(t->representatives));
 t->magic=MAGIC;t->phase=M16_GC_INDEX;t->values=values;t->bin=32;return 0;
}
int m16_geometry_cache_step(m16_geometry_cache *t){
 unsigned end;if(!valid(t))return M16_GC_INVALID;t->last_items=0;t->steps++;
 if(t->phase==M16_GC_FAILED)return t->error;
 if(t->phase==M16_GC_READY)return 0;
 if(!environment())return fail(t,M16_GC_ENVIRONMENT);
 if(t->phase==M16_GC_INDEX){
  end=t->cursor+64u;if(end>10400u)end=10400u;
  for(unsigned i=t->cursor;i<end;i++){
   unsigned id=m16_signature_dictionary_codes[i]&0x7fffu;
   if(id>=M16_GC_LAGS)return fail(t,M16_GC_GEOMETRY);
   if(t->representatives[id]==UINT16_MAX){t->representatives[id]=(uint16_t)i;t->indexed++;}
   t->last_items++;
  }
  t->cursor=end;if(end==10400u){if(t->indexed!=M16_GC_LAGS)return fail(t,M16_GC_GEOMETRY);t->phase=M16_GC_BUILD;}
 }else if(t->phase==M16_GC_BUILD){
  for(unsigned i=0;i<16u&&t->lag<M16_GC_LAGS;i++){
   unsigned at=t->representatives[t->lag],code=m16_signature_dictionary_codes[at];float ri[2];uint32_t re,im;
   if(at>=10400u||mic16_spatial_signature(at/32u,at%32u,t->bin,ri))return fail(t,M16_GC_GEOMETRY);
   memcpy(&re,ri,4);memcpy(&im,ri+1,4);im^=(code&0x8000u)<<16;
   t->values[t->lag*M16_GC_BINS+t->bin-32u]=re|((uint64_t)im<<32);t->built_pairs++;t->last_items++;
   if(++t->bin==839u){t->bin=32u;t->lag++;}
  }
  if(t->lag==M16_GC_LAGS)t->phase=M16_GC_READY;
 }else return fail(t,M16_GC_INVALID);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return t->phase==M16_GC_READY?0:1;
}
int m16_geometry_cache_lookup(void *context,unsigned grid,unsigned pair,unsigned bin,float *ri){
 m16_geometry_cache *t=(m16_geometry_cache *)context;unsigned code;uint64_t word;uint32_t re,im;
 if(!valid(t)||!ri||grid>=325u||pair>=32u||bin<32u||bin>838u)return M16_GC_INVALID;
 if(t->phase!=M16_GC_READY)return M16_GC_BUSY;
 if(!environment())return M16_GC_ENVIRONMENT;
 code=m16_signature_dictionary_codes[grid*32u+pair];word=t->values[(code&0x7fffu)*M16_GC_BINS+bin-32u];
 re=(uint32_t)word;im=(uint32_t)(word>>32)^((code&0x8000u)<<16);
 memcpy(ri,&re,4);memcpy(ri+1,&im,4);t->lookup_calls++;return 0;
}

int m16_geometry_cache_row(void *context,unsigned grid,unsigned pair,const uint64_t **row,uint64_t *sign_mask){
 m16_geometry_cache *t=(m16_geometry_cache *)context;unsigned code;
 if(!valid(t)||!row||!sign_mask||grid>=325u||pair>=32u)return M16_GC_INVALID;
 if(t->phase!=M16_GC_READY)return M16_GC_BUSY;
 if(!environment())return M16_GC_ENVIRONMENT;
 code=m16_signature_dictionary_codes[grid*32u+pair];*row=t->values+(code&0x7fffu)*M16_GC_BINS;
 *sign_mask=(uint64_t)(code&0x8000u)<<48;t->lookup_calls++;return 0;
}
