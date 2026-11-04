/* 独立连续采集诊断：只核验原始输入与队列，不调用C16、不判断声源。
 * 配合现有网络/视频协作服务；没有受控声源环境也能检查DMA连续性。
 * 新GPIO状态接线未生成匹配硬件之前，默认开关阻止启动采集。 */
#include "mic16_irq_capture_arm.h"
#include "mic16_irq_capture_site.h"
#include "mic16_av_runtime_arm.h"
#include "mic16_network.h"
#include "../modules/mic16_cooperate.h"
#include "xil_cache.h"
#include "xtime_l.h"
#include <stdio.h>
static unsigned char irq_raw_windows[3u*2560u*1024u] __attribute__((aligned(64)));
static mic16_irq_capture_arm capture __attribute__((aligned(64)));
typedef struct {
 unsigned signature,version,windows,digest[32],nonzero[32],stop_confirmed,quarantined;
 int code;
 uint64_t timer_hz,consume_ticks[32],irq_count,max_irq_gap,max_handler,packets;
} irq_diagnostic_result;
volatile irq_diagnostic_result g_mic16_irq_diagnostic;
int main(void){
 XTime begin,end,stop_begin;mic16_irq_lease lease;unsigned i,j,digest,nonzero;int rc;
 Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
 g_mic16_irq_diagnostic.signature=0x4d494442;g_mic16_irq_diagnostic.version=20261021;
 g_mic16_irq_diagnostic.timer_hz=COUNTS_PER_SECOND;
 printf("\r\nMIC16 CONTINUOUS DMA DIAGNOSTIC ONLY NO_C16 NO_SOURCE_RESULTS\r\n");
 rc=m16_runtime_arm_start();
 if(!rc)rc=mic16_irq_capture_arm_open(&capture,irq_raw_windows,sizeof(irq_raw_windows),1u);
 if(rc)goto cleanup;
 for(;;){
  M16_COOPERATE();rc=mic16_irq_capture_arm_take(&capture,&lease);
  if(rc==M16_IRQ_EMPTY)continue;
  if(rc==M16_IRQ_DONE){rc=0;break;}
  if(rc)break;
  if(lease.window>=32){rc=M16_IRQ_COUNTER;break;}
  XTime_GetTime(&begin);digest=2166136261u;nonzero=0;
  for(i=0;i<2560;i++){
   const unsigned char *p=mic16_irq_capture_arm_packet(&capture,&lease,i);
   if(!p){rc=M16_IRQ_LEASE;break;}
   for(j=0;j<1024;j++){digest=(digest^p[j])*16777619u;nonzero+=p[j]!=0;}
   if((i&15u)==0)M16_COOPERATE();
  }
  XTime_GetTime(&end);
  if(mic16_irq_capture_arm_release(&capture,&lease) && !rc)rc=M16_IRQ_LEASE;
  if(rc)break;
  g_mic16_irq_diagnostic.digest[lease.window]=digest;g_mic16_irq_diagnostic.nonzero[lease.window]=nonzero;
  g_mic16_irq_diagnostic.consume_ticks[lease.window]=end-begin;g_mic16_irq_diagnostic.windows++;
 }
cleanup:
 XTime_GetTime(&stop_begin);
 while(!mic16_irq_capture_arm_stop(&capture)){
  M16_COOPERATE();XTime_GetTime(&end);if(end-stop_begin>COUNTS_PER_SECOND/5u)break;
 }
 if(!capture.stopped && capture.ring.magic && !rc)rc=M16_IRQ_DMA;
 g_mic16_irq_diagnostic.code=rc;g_mic16_irq_diagnostic.stop_confirmed=capture.stopped;
 g_mic16_irq_diagnostic.quarantined=capture.ring.quarantined||capture.reset_failed;
 g_mic16_irq_diagnostic.irq_count=capture.irq_count;g_mic16_irq_diagnostic.max_irq_gap=capture.max_irq_gap_ticks;
 g_mic16_irq_diagnostic.max_handler=capture.max_handler_ticks;g_mic16_irq_diagnostic.packets=capture.ring.packets;
 /* DMA已停止且无隔离时才刷新归属CPU的首窗，供只读检查数字格式。 */
 if(capture.stopped && !g_mic16_irq_diagnostic.quarantined && !rc)
  for(i=0;i<2560;i++){Xil_DCacheFlushRange((INTPTR)(irq_raw_windows+i*1024u),1024);if((i&15u)==0)M16_COOPERATE();}
 printf("LIVE_INPUT_PROBE_RAW address=%08lx bytes=2621440 NO_MODEL_NO_SOURCE_RESULT\r\n",(unsigned long)irq_raw_windows);
 Xil_DCacheFlushRange((INTPTR)&g_mic16_irq_diagnostic,sizeof(g_mic16_irq_diagnostic));
 printf("IRQ_CAPTURE_END rc=%d windows=%u packets=%llu stop=%u quarantine=%u timer_hz=%llu max_handler_ticks=%llu max_gap_ticks=%llu NOT_ALGORITHM_OR_REALTIME_ACCEPTANCE\r\n",
  rc,g_mic16_irq_diagnostic.windows,capture.ring.packets,capture.stopped,g_mic16_irq_diagnostic.quarantined,
  (unsigned long long)COUNTS_PER_SECOND,capture.max_handler_ticks,capture.max_irq_gap_ticks);
 if(rc==M16_IRQ_NOT_WIRED)printf("NEEDS_MATCHED_GPIO_STATUS_BIT_XSA_AND_WIRING_APPROVAL; CAPTURE_NOT_STARTED\r\n");
 for(;;){M16_COOPERATE();}
}
