/* 完整候选的扫描后端；仅由full_scanner构建选择，默认CPU发布包保持不变。 */
#include "mic16_scanner_hls.h"
static m16_scanner_workspace backend __attribute__((aligned(64)));
typedef struct {
    unsigned calls,failed_calls;int last_error;
    uint64_t refinement_ticks;
    m16_scanner_report scanner;
    m16_scanner_refinement refinement;
} m16_scanner_runtime;
volatile m16_scanner_runtime g_mic16_hls_scanner __attribute__((aligned(64)));
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;
}
int mic16_spatial_project(const float *x,float *maps,mic16_spatial_workspace *w){
    m16_scanner_report scan={0};m16_scanner_refinement refinement={0};
    uint64_t begin=0,end=0;int rc;
    if(!x || !maps || !w || overlap(x,M16_SP_PHAT_FLOATS*4ul,maps,M16_SP_MAP_FLOATS*4ul) ||
       overlap(x,M16_SP_PHAT_FLOATS*4ul,w,sizeof(*w)) || overlap(maps,M16_SP_MAP_FLOATS*4ul,w,sizeof(*w)))return M16_SCAN_INVALID;
    if(!backend.initialized){rc=m16_scanner_platform_init(&backend);if(rc)return rc;}
    g_mic16_hls_scanner.calls++;
    rc=m16_scanner_project(&backend,x,maps,&scan);g_mic16_hls_scanner.scanner=scan;
    if(!rc){
        begin=backend.io.ticks(backend.io.context);
        rc=m16_scanner_refine(x,maps,w,&refinement);
        end=backend.io.ticks(backend.io.context);
        if(end<begin)rc=M16_SCAN_CLOCK;
        else g_mic16_hls_scanner.refinement_ticks=end-begin;
        if(!rc)g_mic16_hls_scanner.refinement=refinement;
    }
    g_mic16_hls_scanner.last_error=rc;
    if(rc)g_mic16_hls_scanner.failed_calls++;
    backend.io.flush(backend.io.context,(const void *)&g_mic16_hls_scanner,sizeof(g_mic16_hls_scanner));
    backend.io.barrier(backend.io.context);return rc;
}
