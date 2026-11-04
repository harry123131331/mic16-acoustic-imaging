/* 当前V6完整任务图固定输入计时：只读固化数字输入，不启动麦克风DMA。
 * 首次运行单列，随后关闭/打开/打开/关闭阶段计时；与正式常驻使用同一BIT/XSA/BSP。 */
#include "mic16_graph_benchmark.h"
#include "mic16_audio_platform_arm.h"
#include "mic16_network.h"
#include "mic16_network_site.h"
#include "mic16_simd_fft.h"
#include "xtime_l.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xparameters.h"
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
extern const unsigned char _binary_cached_reference_f32_bin_start[];
extern const unsigned char _binary_fixed_input_f32_bin_start[];
extern const unsigned char _binary_fixed_input_f32_bin_end[];
static m16_audio_platform_arm platform;
static m16_graph_benchmark benchmark;
m16_graph_benchmark_report g_m16_graph_benchmark_report __attribute__((aligned(64)))={.magic=0x47423631u,.version=2};
volatile uint32_t g_m16_benchmark_environment[8];
_Static_assert(sizeof(m16_graph_benchmark_run)==6224,"计时记录布局改变");
_Static_assert(sizeof(m16_graph_benchmark_report)==31176,"计时报文布局改变");
_Static_assert(offsetof(m16_graph_benchmark_report,run)==56,"计时数组起点改变");
static uint64_t ticks(void *unused){XTime t;(void)unused;XTime_GetTime(&t);return t;}
static uint64_t ms(void){return ticks(0)/(COUNTS_PER_SECOND/1000u);}
/* 只发布诊断记录，不在计时期间清扫整个数据缓存，避免扰动工作表。 */
static void publish(void){
 Xil_DCacheFlushRange((INTPTR)&g_m16_graph_benchmark_report,sizeof(g_m16_graph_benchmark_report));
 Xil_DCacheFlushRange((INTPTR)g_m16_benchmark_environment,sizeof(g_m16_benchmark_environment));
 __asm__ volatile("dsb sy":::"memory");
}
static void service(void){
 m16_network_poll();
 if(platform.ready)(void)m16_control_udp_step(&platform.control_udp,ms());

}
int main(void){
 const m16_network_config network=M16_NETWORK_SITE_CONFIG;
 const float *reference=(const float *)_binary_cached_reference_f32_bin_start;
 const float *input=(const float *)_binary_fixed_input_f32_bin_start;
 m16_window_capture_config cfg={0};unsigned fpscr,reported=0;int rc;
 m16_graph_benchmark_io io={0,ticks,0,COUNTS_PER_SECOND,0};
 __asm__ volatile("vmrs %0,fpscr":"=r"(fpscr));fpscr&=~0x01c00000u;__asm__ volatile("vmsr fpscr,%0"::"r"(fpscr));
 g_m16_benchmark_environment[0]=0x454e5636u;g_m16_benchmark_environment[1]=fpscr;
 __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(fpscr));g_m16_benchmark_environment[2]=fpscr;
 setvbuf(stdout,0,_IONBF,0);m16_simd_fft_select(2);m16_simd_fft_clear_tables();Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR,0);
 if((uintptr_t)input%64u||(uintptr_t)_binary_fixed_input_f32_bin_end-(uintptr_t)input!=40960u*16u*4u){rc=M16_GB_INVALID;goto failed;}
 rc=m16_network_init_resident(&network);if(rc)goto failed;
 cfg.scale_exponent=18;cfg.warmup_windows=20;cfg.transfer_timeout_ticks=3ULL*COUNTS_PER_SECOND;
 cfg.reset_timeout_ticks=COUNTS_PER_SECOND/10;cfg.packet_samples=2048;cfg.dma_length_bits=23;cfg.fifo_streaming=cfg.local_fifo_reset=1;
 for(unsigned i=0;i<16;i++)cfg.logical_to_wire[i]=i;
 rc=m16_audio_platform_arm_init(&platform,&cfg,reference,"v6_fixed_profile",123);if(rc)goto failed;
 uint64_t initial=ticks(0);
 while(!platform.ready){service();rc=m16_audio_platform_arm_step(&platform);if(rc<0)goto failed;}
 g_m16_benchmark_environment[3]=1;
 /* 能力为0，拒绝上位机START；初始化和计时都不调用采集启动。 */
 platform.control.capabilities=0;
 rc=m16_graph_benchmark_init(&benchmark,&g_m16_graph_benchmark_report,&platform.pipeline,&platform.shared_graph,
  platform.capture_stream.front->ring,platform.capture_stream.front_context,input,reference,&io);if(rc)goto failed;
 publish();
 printf("V6_GRAPH_BENCHMARK_READY startup_ticks=%llu timer_hz=%lu NO_DMA_NO_CAPTURE\r\n",ticks(0)-initial,(unsigned long)COUNTS_PER_SECOND);
 for(;;){
  uint64_t a=ticks(0);unsigned slices=0;service();
  do {
   rc=m16_graph_benchmark_step(&benchmark);
   if(rc!=1)break;
   if(benchmark.phase!=M16_GB_START&&benchmark.phase!=M16_GB_DRAIN&&benchmark.run<M16_GB_RUNS&&
    ticks(0)-g_m16_graph_benchmark_report.run[benchmark.run].started>300ULL*COUNTS_PER_SECOND)
    (void)m16_graph_benchmark_abort(&benchmark,M16_GB_TIMEOUT);
  }while(++slices<32u&&ticks(0)-a<COUNTS_PER_SECOND/2000u);
  if(g_m16_graph_benchmark_report.runs!=reported){
   reported=g_m16_graph_benchmark_report.runs;const m16_graph_benchmark_run *v=&g_m16_graph_benchmark_report.run[reported-1];
   publish();
   printf("V6_GRAPH_RUN run=%u instrumented=%lu front=%llu snapshot=%llu detect=%llu normal=%llu flush=%llu total=%llu crc=%08lx candidates=%lu sources=%lu FFT=%llu FIR=%llu\r\n",
    reported,(unsigned long)v->instrumented,v->front_ticks,v->snapshot_ticks,v->detection_ticks,v->normal_ticks,v->flush_ticks,v->total_ticks,
    (unsigned long)v->crc,(unsigned long)v->candidates,(unsigned long)v->sources,v->FFT_jobs,v->FIR_jobs);
  }
  if(g_m16_graph_benchmark_report.done)publish();
  if(rc<=0)break;
 }
 publish();
 printf("V6_GRAPH_BENCHMARK_END done=%lu error=%ld owners_released=%lu FIXED_INPUT_NOT_REALTIME_ACCEPTANCE\r\n",
  (unsigned long)g_m16_graph_benchmark_report.done,(long)g_m16_graph_benchmark_report.error,(unsigned long)g_m16_graph_benchmark_report.owners_released);
 for(;;)service();
failed:
 g_m16_graph_benchmark_report.error=(uint32_t)rc;g_m16_graph_benchmark_report.done=2;g_m16_graph_benchmark_report.phase=M16_GB_FAILED;
 publish();printf("V6_GRAPH_BENCHMARK_INIT_FAIL code=%d NO_CAPTURE\r\n",rc);for(;;)service();
}
