#include "mic16_av_runtime_arm.h"
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
#include "mic16_av_runtime.h"
#include "mic16_av_runtime_site.h"
#include "mic16_av_site.h"
#include "mic16_network.h"
#include "mic16_network_site.h"
#include "xparameters.h"
#include "xtime_l.h"
#include <stdio.h>
static m16_av_runtime runtime;
static m16_video_export video;
static m16_video_device device;
/* 不与相机三帧区重叠，两个CPU快照各768000字节，链接审计另行核查。 */
static unsigned char video_snapshots[2][800u*480u*2u] __attribute__((aligned(64)));
static volatile unsigned site_network_verified=M16_NETWORK_SITE_VERIFIED;
static volatile unsigned site_video_enabled=M16_PC_VIDEO_ENABLED;
static unsigned started,network_attempted,irq_saved,capture_paused;
static int network_code=-99,video_code=-99;
static uint64_t now_us(void *context){
 XTime t;(void)context;XTime_GetTime(&t);
 return (t/COUNTS_PER_SECOND)*1000000ULL+(t%COUNTS_PER_SECOND)*1000000ULL/COUNTS_PER_SECOND;
}
static void poll_network(void *context){(void)context;m16_network_poll();}
static int send_video(void *context,const unsigned char *p,unsigned n){
 (void)context;return m16_network_try_send_to(M16_PC_VIDEO_PORT,p,n);
}
int m16_runtime_arm_network(void){
 const m16_network_config config=M16_NETWORK_SITE_CONFIG;
 if(!site_network_verified)return -1;
 if(!network_attempted){network_attempted=1;network_code=m16_network_init(&config);}
 return network_code;
}
int m16_runtime_arm_start(void){
 m16_av_runtime_ops ops={0,now_us,poll_network,send_video};m16_video_export *selected=0;
 if(started)return -1;
 /* 初始化期间尚未注册检查点，ARP等待不会重入服务。 */
 if(!m16_runtime_arm_network()&&site_video_enabled){
  video_code=m16_video_export_arm_open(&device,XPAR_CAMERA_BASEADDR);
  if(!video_code)video_code=m16_video_export_init(&video,&device,video_snapshots[0],video_snapshots[1],
   sizeof(video_snapshots[0]),M16_PC_VIDEO_EPOCH,MIC16_AV_MOUNT_ID,MIC16_AV_MODE_ID,
   M16_PC_VIDEO_TRANSFORM_ID,send_video,0);
  if(!video_code)selected=&video;
 }
 if(m16_av_runtime_init(&runtime,selected,&ops,M16_IO_RATE_BYTES,M16_IO_BURST_BYTES,M16_IO_INTERVAL_US))return -1;
 started=1;
 if(m16_cooperate_bind(m16_av_runtime_step,&runtime))return -1;
 m16_runtime_arm_diagnostics();return 0;
}
void m16_runtime_arm_service(void){if(started)m16_av_runtime_step(&runtime);}
int m16_runtime_arm_pause_capture(void){
 unsigned state;
 if(!started||capture_paused||m16_av_runtime_pause(&runtime))return -1;
 /* 原音频采用同步轮询小包；采集期间暂停IRQ，结束时精确恢复原屏蔽状态。
  * HDMI/相机的PL通路独立运行。网口在此阶段可能丢接收包，不能宣称音视频持续回传。 */
 __asm__ volatile("mrs %0,cpsr\n\tcpsid i":"=r"(state)::"memory");
 irq_saved=state&(1u<<7);capture_paused=1;return 0;
}
void m16_runtime_arm_resume_capture(void){
 if(!capture_paused)return;
 capture_paused=0;
 if(!irq_saved)__asm__ volatile("cpsie i" ::: "memory");
 (void)m16_av_runtime_resume(&runtime);
}
int m16_runtime_arm_audio_send(void *context,const unsigned char *p,unsigned n){
 int rc;(void)context;
 if(!started||m16_av_runtime_audio_begin(&runtime))return -1;
 rc=m16_network_send(0,p,n);
 rc=m16_av_runtime_audio_end(&runtime,n,rc);M16_COOPERATE();return rc;
}
void m16_runtime_arm_diagnostics(void){
 printf("AV_IO cooperative=%u network_rc=%d video_rc=%d paused=%u fault=%u checkpoints=%llu steps=%llu gap_us=%llu service_us=%llu video_packets=%lu video_frames=%lu dropped=%lu audio_packets=%lu NOT_REALTIME_ACCEPTANCE\r\n",
  started,network_code,video_code,runtime.paused,runtime.fault,runtime.stats.checkpoints,runtime.stats.steps,
  runtime.stats.max_checkpoint_gap_us,runtime.stats.max_service_us,(unsigned long)video.submitted_packets,
  (unsigned long)video.submitted_frames,(unsigned long)video.dropped,(unsigned long)runtime.stats.audio_packets);
}
#endif
