/* 控制流故障注入：只验加速作用域与错误传播，不冒充数值或硬件验收。 */
#include "mic16_application_acceleration.h"
extern int printf(const char *,...);
extern void *memset(void *,int,__SIZE_TYPE__);
#if M16_APP_ACCEL_MODE==3
#include "mic16_shared_spectrum_dispatch.h"
m16_shared_dispatch_report m16_shared_dispatch_counts;
#endif
unsigned m16_fft_fast_mode,m16_fft_fast_calls[3],m16_feature_fast_mode,m16_feature_fast_calls[3];
unsigned m16_dataflow_mode,m16_dataflow_calls[6],m16_magnitude_enabled,m16_real_fft_mode;
unsigned m16_simd_fft_enabled,m16_simd_fft_calls[4],m16_simd_fft_vector_groups,m16_simd_fft_preserve_public=1;
unsigned m16_resample_pipeline_enabled,m16_resample_pipeline_calls[4],m16_beam_tiles_enabled;
static int checks,errors,env_ok=1,inner_rc,recurse,backend_calls,begin_calls,end_calls,cache_active,selection_failure,hardware_error;
static unsigned shared_enabled;
#if M16_APP_STATISTICS_NEON
unsigned m16_statistics_neon_mode,m16_statistics_neon_calls[4];
static int statistics_failure;
int m16_statistics_neon_select(unsigned v){m16_statistics_neon_mode=v;memset(m16_statistics_neon_calls,0,sizeof(m16_statistics_neon_calls));return v && statistics_failure?-1:0;}
#endif
static void check(int ok,const char *text){checks++;if(!ok){errors++;printf("FAIL %s\n",text);}}
int m16_application_test_environment(unsigned *f,unsigned *c){*f=0;*c=15u<<20;return env_ok;}
int m16_fft_fast_select(unsigned v){m16_fft_fast_mode=v;memset(m16_fft_fast_calls,0,sizeof(m16_fft_fast_calls));return 0;}
int m16_feature_fast_select(unsigned v){m16_feature_fast_mode=v;return 0;}
int m16_dataflow_select(unsigned v){m16_dataflow_mode=v;return 0;}
int m16_magnitude_select(unsigned v){m16_magnitude_enabled=v;return 0;}
int m16_real_fft_select(unsigned v){m16_real_fft_mode=v;return 0;}
int m16_simd_fft_select(unsigned v){m16_simd_fft_enabled=v;return v && selection_failure?-1:0;}
int m16_resample_pipeline_select(unsigned v){m16_resample_pipeline_enabled=v;return 0;}
int m16_beam_tiles_select(unsigned v){m16_beam_tiles_enabled=v;return 0;}
void m16_dataflow_begin(void){begin_calls++;cache_active=1;}
void m16_dataflow_end(void){end_calls++;cache_active=0;}
#if M16_APP_ACCEL_MODE==3
int m16_shared_runtime_platform_init(m16_shared_runtime *p){p->initialized=1;return hardware_error;}
int m16_shared_dispatch_bind(m16_shared_runtime *p){return p->initialized?0:-1;}
int m16_shared_dispatch_select(unsigned m){shared_enabled=m;return 0;}
void m16_shared_dispatch_clear(void){memset(&m16_shared_dispatch_counts,0,sizeof(m16_shared_dispatch_counts));}
#endif
static int clear(void){return !m16_fft_fast_mode && !m16_feature_fast_mode && !m16_dataflow_mode &&
 !m16_magnitude_enabled && !m16_real_fft_mode && !m16_simd_fft_enabled && !m16_resample_pipeline_enabled && !m16_beam_tiles_enabled && !shared_enabled
#if M16_APP_STATISTICS_NEON
 && !m16_statistics_neon_mode
#endif
 ;}
int mic16_pipeline_observe_batched_timed(const float *x,const float *q,mic16_pipeline_observation *o,
 mic16_pipeline_workspace *w,const mic16_profile_clock *clock,mic16_pipeline_profile *profile){
 (void)x;(void)q;(void)o;(void)w;(void)clock;(void)profile;backend_calls++;
 check(g_mic16_application_acceleration.active && cache_active,"调用原检测时保持独占及每窗缓存");
 check(m16_fft_fast_mode==3 && m16_feature_fast_mode==3 && m16_dataflow_mode==2 &&
  m16_magnitude_enabled==1 && m16_real_fft_mode==3 && m16_simd_fft_enabled==2 &&
  m16_resample_pipeline_enabled==1 && !m16_beam_tiles_enabled,"只启用已核验组合");
#if M16_APP_ACCEL_MODE==3
 check(shared_enabled==1,"共享FFT仅在模式3检测内启用");
 m16_shared_dispatch_counts.calls=16;m16_shared_dispatch_counts.completed=16;
 m16_shared_dispatch_counts.batches=32;m16_shared_dispatch_counts.total_ticks=123;
#endif
#if M16_APP_STATISTICS_NEON
 check(m16_statistics_neon_mode==1,"统计向量化只在检测内启用");
 m16_statistics_neon_calls[0]=1;m16_statistics_neon_calls[2]=256;
#endif
 if(recurse){check(__wrap_mic16_pipeline_observe_batched_timed(x,q,o,w,clock,profile)==-120,"递归拒绝");check(cache_active && m16_simd_fft_enabled==2,"拒绝递归不清外层状态");}
 m16_fft_fast_calls[0]=24;m16_dataflow_calls[3]=30;m16_simd_fft_vector_groups=7;
 return inner_rc;
}
static int run(void){return __wrap_mic16_pipeline_observe_batched_timed(0,0,0,0,0,0);}
int main(int argc,char **argv){
 int before;(void)argv;(void)hardware_error;
 if(argc==2){
#if M16_APP_ACCEL_MODE==3
  hardware_error=-6;check(run()==-6,"硬件初始化失败原样传播");check(clear() && !backend_calls,"硬件失败不执行检测");
  hardware_error=0;check(run()==-6 && !backend_calls,"硬件失败不静默重新初始化");
#endif
 }else{
  check(run()==0,"正常返回");check(clear() && !cache_active,"检测后增强回到原算术");
  check(g_mic16_application_acceleration.vector_groups==7 && g_mic16_application_acceleration.fft[0]==24,"计数在关闭模式之前保存");
  check(begin_calls==1 && end_calls==1,"每窗缓存成对");
#if M16_APP_STATISTICS_NEON
  check(g_mic16_application_acceleration.statistics_enabled && g_mic16_application_acceleration.statistics[2]==256,"统计执行计数在恢复前保存");
#endif
  inner_rc=-6;check(run()==-6,"原检测错误不隐式回退");check(clear() && !cache_active,"检测错误也恢复增强环境");inner_rc=0;
  recurse=1;check(run()==0,"外层检测不被重入破坏");recurse=0;check(clear(),"重入测试后恢复");
  env_ok=0;before=backend_calls;check(run()==-121,"不兼容FPU环境停止");check(backend_calls==before && clear(),"环境失败不计算也不修改FPU");env_ok=1;
  selection_failure=1;check(run()==-122,"配置失败停止");check(backend_calls==before && clear(),"部分配置失败清理");selection_failure=0;
  m16_dataflow_mode=2;before=backend_calls;check(run()==-120,"外部算术所有者冲突拒绝");check(m16_dataflow_mode==2 && backend_calls==before,"不改变外部所有者状态");m16_dataflow_mode=0;
#if M16_APP_STATISTICS_NEON
  statistics_failure=1;before=backend_calls;check(run()==-122 && backend_calls==before && clear(),"统计配置失败清理且不执行检测");statistics_failure=0;
  m16_statistics_neon_mode=1;check(run()==-120 && m16_statistics_neon_mode==1,"统计外部所有者冲突不被清理");m16_statistics_neon_mode=0;
#endif
  check(run()==0 && clear(),"后续调用无需重启程序");check(begin_calls==end_calls,"所有已进入窗口均关闭缓存");
 }
 printf("APPLICATION_ACCEL_SCOPE mode=%d checks=%d errors=%d HOST_MOCK_NOT_HARDWARE_NUMERICS\n",M16_APP_ACCEL_MODE,checks,errors);
 return errors?1:0;
}
