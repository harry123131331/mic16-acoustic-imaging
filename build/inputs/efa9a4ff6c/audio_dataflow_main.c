/* 纯音频常驻入口：首次加载后等待上位机START，不自动采背景，不限定三窗。 */
#include "mic16_audio_platform_arm.h"
#include "mic16_network.h"
#include "mic16_network_site.h"
#include "mic16_simd_fft.h"
#include "xtime_l.h"
#include "xil_io.h"
#include "xparameters.h"
#include <stdio.h>
#include <stdint.h>
extern const unsigned char _binary_cached_reference_f32_bin_start[];
extern const unsigned char _binary_cached_reference_f32_bin_end[];
static m16_audio_platform_arm platform;
/* 这是当前原型已使用的数字尺度及名义接线，逐麦克风空间顺序尚需实板验证。
 * 新包清单必须绑定该配置与原始参考哈希；不能称为声压或声像标定。 */
static const unsigned char channel_map[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const char reference_key[]="nominal_pose:1/cached_reference:20261023";
typedef struct {uint32_t magic,ready,boot;int error,control_error;uint64_t iterations;} resident_diagnostics;
volatile resident_diagnostics g_m16_audio_resident={0x4d523236u,0,0,0,0,0};
static uint64_t ticks(void){XTime value;XTime_GetTime(&value);return value;}
static uint64_t milliseconds(void){uint64_t v=ticks();return v/COUNTS_PER_SECOND*1000u+(v%COUNTS_PER_SECOND)*1000u/COUNTS_PER_SECOND;}
int main(void){
 const m16_network_config network=M16_NETWORK_SITE_CONFIG;
 const float *reference=(const float *)_binary_cached_reference_f32_bin_start;
 m16_window_capture_config cfg={0};uint64_t timestamp;unsigned fpscr;int rc,reported=0;
 /* 自编数值节点要求舍入到最近、不启用FTZ；保留其它浮点状态位。 */
 __asm__ volatile("vmrs %0,fpscr":"=r"(fpscr));fpscr&=~0x01c00000u;__asm__ volatile("vmsr fpscr,%0"::"r"(fpscr));
 m16_simd_fft_select(2);m16_simd_fft_clear_tables();
 Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR,0);
 if((uintptr_t)reference%64u||(uintptr_t)_binary_cached_reference_f32_bin_end-(uintptr_t)reference!=8192u*16u*4u)return -1400;
 for(unsigned i=0;i<8192u*16u;i++)if(!(reference[i]>=-1.f&&reference[i]<=1.f))return -1401;
 rc=m16_network_init_resident(&network);if(rc)return rc;
 /* 启动标识取实际计时器，不把编译日期当作每次启动编号；协议另有电脑nonce及递增epoch。
  * 该标识不是密码学随机数，冷启动碰撞风险需在统一上板测试中记录。 */
 timestamp=ticks();g_m16_audio_resident.boot=(uint32_t)(timestamp^(timestamp>>32));
 if(!g_m16_audio_resident.boot)g_m16_audio_resident.boot=1;
 cfg.scale_exponent=18;cfg.warmup_windows=1;cfg.transfer_timeout_ticks=3ULL*COUNTS_PER_SECOND;
 cfg.reset_timeout_ticks=COUNTS_PER_SECOND/10u;cfg.packet_samples=40960;cfg.dma_length_bits=23;cfg.fifo_streaming=1;cfg.local_fifo_reset=1;
 for(unsigned i=0;i<16;i++)cfg.logical_to_wire[i]=channel_map[i];
 rc=m16_audio_platform_arm_init(&platform,&cfg,reference,reference_key,g_m16_audio_resident.boot);
 if(rc){g_m16_audio_resident.error=rc;return rc;}
 for(;;){
  /* 一次有限网络服务、一个控制请求/回复、一个计算图片段；不在控制回调里运行算法。 */
  m16_network_poll();
  if(platform.ready){rc=m16_control_udp_step(&platform.control_udp,milliseconds());if(rc<0)g_m16_audio_resident.control_error=rc;}
  rc=m16_audio_platform_arm_step(&platform);
  g_m16_audio_resident.iterations++;g_m16_audio_resident.ready=platform.ready;
  if(rc<0){
   g_m16_audio_resident.error=rc;
   if(!reported){reported=1;printf("AUDIO_DATAFLOW_FAULT code=%d; draining owners, not realtime acceptance\r\n",rc);}
   /* 计算运行期故障必须继续服务排空；没有用return/重载来假装设备停止。 */
  }
 }
}
