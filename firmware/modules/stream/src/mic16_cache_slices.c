#include "mic16_cache_slices.h"
int mic16_cache_flush_slices(uintptr_t address,unsigned bytes,void (*flush)(void *,uintptr_t,unsigned),int (*checkpoint)(void *),void *ctx){
 unsigned count;
 if(!flush || !checkpoint || (address&63u) || address>UINTPTR_MAX-bytes)return -1;
 while(bytes){count=bytes>1024u?1024u:bytes;flush(ctx,address,count);address+=count;bytes-=count;if(checkpoint(ctx))return -2;}
 return 0;
}
