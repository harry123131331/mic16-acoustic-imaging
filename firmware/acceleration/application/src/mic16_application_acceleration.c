/* 将已核验的检测加速接到完整应用；不接管DMA、GIC、网络、增强或声像坐标。 */
#include "mic16_application_acceleration.h"
#include "mic16_cooperate.h"
#include "mic16_fft_fast.h"
#include "mic16_dataflow_fast.h"
#include "mic16_real_fft.h"
#include "mic16_simd_fft.h"
#include "mic16_resample_pipeline.h"
#include "mic16_beam_tiles.h"
#if M16_APP_STATISTICS_NEON
#include "mic16_statistics_neon.h"
#endif
extern int m16_magnitude_select(unsigned);
extern unsigned m16_magnitude_enabled;
#if M16_APP_ACCEL_MODE==3
#include "mic16_shared_spectrum_dispatch.h"
static m16_shared_runtime shared_state __attribute__((aligned(64)));
#endif
#if M16_APP_ACCEL_MODE!=2 && M16_APP_ACCEL_MODE!=3
#error Application acceleration must explicitly select checked mode 2 or 3
#endif
#if defined(M16_APP_ACCEL_HOST_TEST)
extern int m16_application_test_environment(unsigned *,unsigned *);
#define ORIGINAL mic16_pipeline_observe_batched_timed
#else
#if !defined(__arm__) || !defined(__ARM_NEON)
#error Production acceleration requires actual Cortex-A9 NEON compilation
#endif
#define ORIGINAL __real_mic16_pipeline_observe_batched_timed
#endif
extern int ORIGINAL(const float *,const float *,mic16_pipeline_observation *,
    mic16_pipeline_workspace *,const mic16_profile_clock *,mic16_pipeline_profile *);
volatile mic16_application_acceleration_report g_mic16_application_acceleration;
static int environment(unsigned *fpscr,unsigned *cpacr){
#if defined(M16_APP_ACCEL_HOST_TEST)
    return m16_application_test_environment(fpscr,cpacr);
#else
    __asm__ volatile("vmrs %0, fpscr":"=r"(*fpscr));
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2":"=r"(*cpacr));
    return !(*fpscr&(3u<<22)) && (*cpacr&(15u<<20))==(15u<<20) &&
        !(*cpacr&0xc0000000u) && m16_simd_fft_preserve_public;
#endif
}
static int modes_off(void){
    int rc=0;
#if M16_APP_STATISTICS_NEON
    rc|=m16_statistics_neon_select(0);
#endif
#if M16_APP_ACCEL_MODE==3
    rc|=m16_shared_dispatch_select(0);
#endif
    rc|=m16_fft_fast_select(0);rc|=m16_feature_fast_select(0);
    rc|=m16_dataflow_select(0);rc|=m16_magnitude_select(0);
    rc|=m16_real_fft_select(0);rc|=m16_simd_fft_select(0);
    rc|=m16_resample_pipeline_select(0);rc|=m16_beam_tiles_select(0);
    return rc;
}
static int modes_on(void){
    int rc=0;
    rc|=m16_fft_fast_select(3);rc|=m16_feature_fast_select(3);
    rc|=m16_dataflow_select(2);rc|=m16_magnitude_select(1);
    rc|=m16_real_fft_select(3);rc|=m16_simd_fft_select(2);
    rc|=m16_resample_pipeline_select(1);rc|=m16_beam_tiles_select(0);
#if M16_APP_STATISTICS_NEON
    rc|=m16_statistics_neon_select(1);
#endif
#if M16_APP_ACCEL_MODE==3
    rc|=m16_shared_dispatch_select(1);m16_shared_dispatch_clear();
#endif
    return rc;
}
static int outside_modes_clear(void){
    return !m16_fft_fast_mode && !m16_feature_fast_mode && !m16_dataflow_mode &&
        !m16_magnitude_enabled && !m16_real_fft_mode && !m16_simd_fft_enabled &&
        !m16_resample_pipeline_enabled && !m16_beam_tiles_enabled
#if M16_APP_STATISTICS_NEON
        && !m16_statistics_neon_mode
#endif
        ;
}
static void snapshot_counts(void){
    unsigned i;
#if M16_APP_STATISTICS_NEON
    for(i=0;i<4;i++)g_mic16_application_acceleration.statistics[i]=m16_statistics_neon_calls[i];
#endif
    for(i=0;i<3;i++)g_mic16_application_acceleration.fft[i]=m16_fft_fast_calls[i];
    for(i=0;i<6;i++)g_mic16_application_acceleration.dataflow[i]=m16_dataflow_calls[i];
    for(i=0;i<4;i++){
        g_mic16_application_acceleration.simd[i]=m16_simd_fft_calls[i];
        g_mic16_application_acceleration.resample[i]=m16_resample_pipeline_calls[i];
    }
    g_mic16_application_acceleration.vector_groups=m16_simd_fft_vector_groups;
#if M16_APP_ACCEL_MODE==3
    g_mic16_application_acceleration.shared_calls=m16_shared_dispatch_counts.calls;
    g_mic16_application_acceleration.shared_completed=m16_shared_dispatch_counts.completed;
    g_mic16_application_acceleration.shared_batches=m16_shared_dispatch_counts.batches;
    g_mic16_application_acceleration.shared_errors=m16_shared_dispatch_counts.errors;
    g_mic16_application_acceleration.shared_cache_ticks=m16_shared_dispatch_counts.cache_ticks;
    g_mic16_application_acceleration.shared_device_ticks=m16_shared_dispatch_counts.device_ticks;
    g_mic16_application_acceleration.shared_total_ticks=m16_shared_dispatch_counts.total_ticks;
#endif
}
int __wrap_mic16_pipeline_observe_batched_timed(const float *x,const float *q,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w,
    const mic16_profile_clock *clock,mic16_pipeline_profile *profile){
    int rc,off;unsigned fpscr=0,cpacr=0;
    /* 不允许网络/显示检查点递归调用检测，或与增强交叉持有全局加速模式。 */
    if(g_mic16_application_acceleration.active || !outside_modes_clear())return -120;
    g_mic16_application_acceleration.active=1;
    g_mic16_application_acceleration.version=20261023u;
    g_mic16_application_acceleration.mode=M16_APP_ACCEL_MODE;
    g_mic16_application_acceleration.statistics_enabled=M16_APP_STATISTICS_NEON;
    g_mic16_application_acceleration.calls++;
    if(!environment(&fpscr,&cpacr)){rc=-121;goto done;}
    g_mic16_application_acceleration.fpscr=fpscr;g_mic16_application_acceleration.cpacr=cpacr;
    if(!g_mic16_application_acceleration.initialized){
        rc=0;
#if M16_APP_ACCEL_MODE==3
        rc=m16_shared_runtime_platform_init(&shared_state);
        if(!rc)rc=m16_shared_dispatch_bind(&shared_state);
#endif
        g_mic16_application_acceleration.initialization_code=rc;
        g_mic16_application_acceleration.initialized=1;
    }
    rc=g_mic16_application_acceleration.initialization_code;
    if(rc)goto done;
    rc=modes_on();if(rc){rc=-122;goto done;}
    m16_dataflow_begin();
    rc=ORIGINAL(x,q,o,w,clock,profile);
    m16_dataflow_end();snapshot_counts();
done:
    /* 检测失败也恢复原模式；不能因共享核故障而静默回退并伪称提速成功。 */
    off=modes_off();if(!rc && off)rc=-123;
    g_mic16_application_acceleration.last_code=rc;
    if(rc)g_mic16_application_acceleration.errors++;
    else g_mic16_application_acceleration.completed++;
    g_mic16_application_acceleration.active=0;
    return rc;
}
