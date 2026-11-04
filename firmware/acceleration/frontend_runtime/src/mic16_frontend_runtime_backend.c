#include "mic16_frontend_runtime.h"
static m16_front_runtime backend __attribute__((aligned(64)));
typedef struct {unsigned calls,failed_calls,fallback_calls;int last_error; m16_front_report frame;} m16_front_runtime_report;
volatile m16_front_runtime_report g_mic16_hls_frontend __attribute__((aligned(64)));
static int overlap(const void *a,unsigned na,const void *b,unsigned nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;
}
unsigned m16_front_hardware_enabled;
int m16_front_hardware_select(unsigned mode){if(mode>1)return -1;m16_front_hardware_enabled=mode;return 0;}
extern int __real_mic16_spectral_process_fft(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
/* 仅显式联合计时程序包装该入口；默认与现有功能演示链接都不引用。 */
int __wrap_mic16_spectral_process_fft(const float *x,mic16_spectral_frame *o,mic16_spectral_workspace *w){
 m16_front_report report={0};int rc;
 if(!m16_front_hardware_enabled)return __real_mic16_spectral_process_fft(x,o,w);
 if(!x || !o || !w || w->initialized!=0x4d313653u ||
  overlap(x,32768u*4u,w,sizeof(*w)) || overlap(o,sizeof(*o),w,sizeof(*w)))return M16_FRONT_INVALID;
 if(!backend.initialized){rc=m16_front_runtime_platform_init(&backend);if(rc)return rc;}
 rc=m16_front_runtime_frame(&backend,x,o,&report);
 /* 只有启动前的可表示范围拒绝可以安全回退；超时/越界均隔离，不能掩盖。 */
 if(rc==M16_FRONT_RANGE){g_mic16_hls_frontend.fallback_calls++;return __real_mic16_spectral_process_fft(x,o,w);}
 g_mic16_hls_frontend.calls++;g_mic16_hls_frontend.last_error=rc;g_mic16_hls_frontend.frame=report;
 if(rc)g_mic16_hls_frontend.failed_calls++;
 return rc;
}
