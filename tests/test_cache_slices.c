#include "mic16_cache_slices.h"
extern int printf(const char *,...);
static unsigned calls,checks,errors,total,fail_after;
static uintptr_t next;
#define CHECK(x) do{checks++;if(!(x))errors++;}while(0)
static void flush(void *p,uintptr_t a,unsigned n){(void)p;CHECK(a==next && !(a&63u) && n>0 && n<=1024);next+=n;total+=n;calls++;}
static int point(void *p){(void)p;return fail_after && calls==fail_after;}
int main(void){unsigned n,k;const unsigned sizes[]={0,16,64,1008,1024,1040,2048,4096,134224};
 for(k=0;k<33;k++){n=k<9?sizes[k]:(k-8)*134224u;calls=total=fail_after=0;next=64;CHECK(!mic16_cache_flush_slices(64,n,flush,point,0));CHECK(total==n && calls==(n+1023u)/1024u);}
 calls=total=0;next=64;fail_after=2;CHECK(mic16_cache_flush_slices(64,8192,flush,point,0)==-2 && calls==2 && total==2048);
 CHECK(mic16_cache_flush_slices(65,1024,flush,point,0)==-1);CHECK(mic16_cache_flush_slices(UINTPTR_MAX-63u,128,flush,point,0)==-1);CHECK(mic16_cache_flush_slices(64,1024,0,point,0)==-1);
 printf("CACHE_SLICES_%s checks=%u errors=%u max_slice=1024 NO_IRQ_LATENCY_MEASUREMENT\n",errors?"FAIL":"PASS",checks,errors);return errors?1:0;}
