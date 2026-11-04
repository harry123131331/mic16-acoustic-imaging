#include "mic16_shared_spectrum_dispatch.h"
static m16_shared_runtime *owner;
static unsigned enabled;
m16_shared_dispatch_report m16_shared_dispatch_counts;
extern int __real_m16_mono_bands_real(const float *,unsigned,double *,double *,mic16_physical_workspace *);
int m16_shared_dispatch_bind(m16_shared_runtime *w){
 if(enabled || (owner && !m16_shared_runtime_available(owner)) || !m16_shared_runtime_available(w))return -1;
 owner=w;return 0;
}
int m16_shared_dispatch_select(unsigned mode){
 if(mode>1 || (mode && !m16_shared_runtime_available(owner)))return -1;
 enabled=mode;return 0;
}
void m16_shared_dispatch_clear(void){m16_shared_dispatch_counts=(m16_shared_dispatch_report){0};}
static int execute(void *context,uint64_t *arena,unsigned n){
 m16_shared_runtime *w=context;m16_shared_report r;int rc;
 if(arena!=w->words)return -1;
 rc=m16_shared_runtime_execute(w,n,&r);
 if(rc)return rc;
 m16_shared_dispatch_counts.batches+=r.completed_batches;
 m16_shared_dispatch_counts.cache_ticks+=r.cache_ticks;
 m16_shared_dispatch_counts.device_ticks+=r.device_ticks;
 m16_shared_dispatch_counts.invalidate_ticks+=r.invalidate_ticks;
 m16_shared_dispatch_counts.total_ticks+=r.total_ticks;return 0;
}
int __wrap_m16_mono_bands_real(const float *x,unsigned n,double *bands,double *levels,mic16_physical_workspace *w){
 int rc;m16_shared_dispatch_counts.calls++;
 if(!enabled){m16_shared_dispatch_counts.cpu_disabled++;return __real_m16_mono_bands_real(x,n,bands,levels,w);}
 /* 必须在任何PS打包写入之前检查所有权；隔离后的DMA区不再复用。 */
 if(!m16_shared_runtime_available(owner)){m16_shared_dispatch_counts.errors++;return -2;}
 rc=m16_shared_mono_bands(x,n,bands,levels,w,owner->words,M16_SHARED_ARENA_WORDS,execute,owner);
 if(rc==1){m16_shared_dispatch_counts.cpu_range++;return __real_m16_mono_bands_real(x,n,bands,levels,w);}
 if(rc){m16_shared_dispatch_counts.errors++;return rc;}
 m16_shared_dispatch_counts.completed++;return 0;
}
