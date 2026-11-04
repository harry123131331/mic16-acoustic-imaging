#include "mic16_statistics_tile_dispatch.h"
static m16_tile_runtime state __attribute__((aligned(64)));
static unsigned active;
unsigned m16_tile_dispatch_enabled;
m16_tile_batch_report m16_tile_dispatch_counts;
m16_tile_runtime_report m16_tile_dispatch_device;
extern int __real_mic16_statistics18_pair_cache(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
static int execute(void *context,unsigned frames,unsigned bins,unsigned candidates,unsigned dirty){
 m16_tile_runtime *w=context;int rc=m16_tile_runtime_execute(w,frames,bins,candidates,dirty);
 m16_tile_dispatch_device.last_control=w->report.last_control;
 m16_tile_dispatch_device.quarantined=w->report.quarantined;
 m16_tile_dispatch_device.kernel_return=w->report.kernel_return;
 if(rc)return rc;
 m16_tile_dispatch_device.cache_ticks+=w->report.cache_ticks;
 m16_tile_dispatch_device.device_ticks+=w->report.device_ticks;
 m16_tile_dispatch_device.invalidate_ticks+=w->report.invalidate_ticks;
 m16_tile_dispatch_device.total_ticks+=w->report.total_ticks;
 m16_tile_dispatch_device.batches++;return 0;
}
int m16_tile_dispatch_init(unsigned abi){
 m16_tile_batch_backend b={&state,state.words,m16_tile_runtime_available,execute};int rc;
 if(active || m16_tile_dispatch_enabled)return M16_TILE_BUSY;
 rc=m16_tile_runtime_platform_init(&state,abi);return rc?rc:m16_tile_batch_bind(&b);
}
int m16_tile_dispatch_select(unsigned mode){
 if(mode>1 || active || (mode && !m16_tile_runtime_available(&state)))return M16_TILE_INVALID;
 m16_tile_dispatch_enabled=mode;m16_tile_dispatch_counts=(m16_tile_batch_report){0};
 m16_tile_dispatch_device=(m16_tile_runtime_report){0};return 0;
}
int __wrap_mic16_statistics18_pair_cache(const double *d,unsigned n,float *out,mic16_extended_workspace *w,mic16_feature_reuse_report *r){
 int rc;
 if(!m16_tile_dispatch_enabled)return __real_mic16_statistics18_pair_cache(d,n,out,w,r);
 /* 不与增强或协作回调交叉持有分块所有权；失败由上层停止该次结果发布。 */
 if(active || !m16_tile_runtime_available(&state))return M16_TILE_BUSY;
 active=1;rc=m16_tile_batch_compute(d,n,out,w,r);
 m16_tile_dispatch_counts=m16_tile_batch_counts;active=0;return rc;
}
