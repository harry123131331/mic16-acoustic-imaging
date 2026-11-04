#include "mic16_audio_platform_arm.h"
#include "mic16_geometry_cache.h"
#include "mic16_network.h"
#include "mic16_network_site.h"
#include "xm16_sp_shared_hw.h"
#include "xm16_shared_projection_engine_hw.h"
#include "xmic16_tf_zone_streaming_hls_q2_24_hw.h"
#include "xparameters.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xtime_l.h"
#include <string.h>
#if !defined(__arm__)
#error 此平台接入文件只用于Cortex-A9，主机测试使用独立设备模型
#endif
/* 这些区段只有最终链接脚本固定地址后才有效；禁止只复制本文件到旧工程运行。 */
static m16_coarse_scan_runtime coarse __attribute__((section(".m16_coarse"),aligned(64)));
static m16_shared_runtime fft_owner __attribute__((section(".m16_fft"),aligned(64)));
static uint64_t projection_arena[40000] __attribute__((section(".m16_projection"),aligned(64)));
static unsigned char capture0[M16_CAPTURE_PACKET_SAMPLES*64u+128u] __attribute__((section(".m16_capture0"),aligned(64)));
static unsigned char capture1[M16_CAPTURE_PACKET_SAMPLES*64u+128u] __attribute__((section(".m16_capture1"),aligned(64)));
static unsigned char capture2[M16_CAPTURE_PACKET_SAMPLES*64u+128u] __attribute__((section(".m16_capture2"),aligned(64)));
static mic16_pipeline_workspace detection_work;
static mic16_pipeline_observation observation;
static m16_statistics_projection_storage statistics;
static m16_enh_workspace enhancement_work;
static m16_cont_workspace continuous_work;
static float output_storage[M16_OUTPUT_POOL_FLOATS];
/* 只增加NOLOAD CPU工作区，表由本ARM原函数上电生成一次，不增加ELF下载负载。 */
static uint64_t geometry_values[M16_GC_WORDS] __attribute__((aligned(64)));
static m16_geometry_cache geometry;
#if M16_CAPTURE_PACKET_SAMPLES == 2048u
/* 环保存20帧历史，后端PCM独立持有；二者均在DDR，不扩大READY积压队列。 */
static m16_front_ring front_ring;
static m16_front_ring_storage front_storage;
static mic16_spectral_workspace front_workspace;
static m16_front_context_task front_context;
static m16_frame_front_stream front_stream;
static float context_pcm[40960u*16u] __attribute__((aligned(64)));
#endif
static unsigned attached;
_Static_assert(XM16_SP_SHARED_CONTROL_ADDR_DDR_ARENA_DATA==0x18,"共享FFT寄存器合同改变");
_Static_assert(XM16_SP_SHARED_CONTROL_ADDR_MODE_DATA==0x3c,"共享FFT模式寄存器改变");
_Static_assert(XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ARENA_DATA==0x18,"投影arena寄存器改变");
_Static_assert(XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_JOBS_DATA==0x34,"投影job寄存器改变");
_Static_assert(XPAR_AXI_DMA_0_BASEADDR==0x40400000u,"DMA地址改变");
_Static_assert(XPAR_PS7_DDR_0_S_AXI_HIGHADDR>=0x1fffffffu,"工作区超出DDR平台配置");
static uint32_t rd(void *base,unsigned offset){return Xil_In32((UINTPTR)base+offset);}
static void wr(void *base,unsigned offset,uint32_t value){Xil_Out32((UINTPTR)base+offset,value);}
static void clean(void *unused,const void *p,unsigned n){(void)unused;Xil_DCacheFlushRange((INTPTR)p,n);}
static void invalidate(void *unused,const void *p,unsigned n){(void)unused;Xil_DCacheInvalidateRange((INTPTR)p,n);}
static uint64_t tick(void *unused){XTime value;(void)unused;XTime_GetTime(&value);return value;}
static void barrier(void *unused){(void)unused;__asm__ volatile("dsb sy":::"memory");}
static int send_packet(void *unused,unsigned port,const unsigned char *data,unsigned bytes){
 (void)unused;return port==5001u?m16_network_try_send_audio(data,bytes):m16_network_try_send_to(port,data,bytes);
}
int m16_audio_platform_arm_init(m16_audio_platform_arm *p,const m16_window_capture_config *provided,
 const float *reference,const char *key,uint32_t boot){
 m16_window_capture_config cfg;m16_scanner_io io;m16_projection_io projection_io;int rc;
 mic16_stream_config schedule={3,3,COUNTS_PER_SECOND,20ULL*COUNTS_PER_SECOND,40ULL*COUNTS_PER_SECOND,1,1};
 if(!p||!provided||provided->packet_samples!=M16_CAPTURE_PACKET_SAMPLES||!reference||!key||!boot||p->initialized||attached||!g_m16_network_stats.initialized)return -1300;
 /* 清软件对象之前先确认三个设备idle，不能覆盖仍被旧设备访问的arena。 */
 for(unsigned i=0;i<3;i++){unsigned ctrl=rd((void *)(UINTPTR)(0x80000000u+i*0x10000u),0);if(!(ctrl&4u)||(ctrl&0x81u))return -1301;}
 if((uintptr_t)coarse.arena.words!=0x02000000u||(uintptr_t)fft_owner.words!=0x04000000u||
  (uintptr_t)projection_arena!=0x06000000u||(uintptr_t)capture0!=0x08000000u||
  (uintptr_t)capture1!=0x08400000u||(uintptr_t)capture2!=0x08800000u)return -1302;
 memset(&coarse,0,sizeof(coarse));memset(&fft_owner,0,sizeof(fft_owner));
 io=(m16_scanner_io){(void *)(UINTPTR)0x80010000u,rd,wr,clean,invalidate,tick,barrier,0,60ULL*COUNTS_PER_SECOND};
 io.arena_address=(uint32_t)(uintptr_t)coarse.arena.words;rc=m16_coarse_scan_init(&coarse,&io);if(rc)return rc;
 io.context=(void *)(UINTPTR)0x80000000u;io.arena_address=(uint32_t)(uintptr_t)fft_owner.words;
 rc=m16_shared_runtime_init(&fft_owner,&io);if(rc)return rc;
 rc=m16_shared_spectrum_task_init(&p->fft_task,&fft_owner);if(rc)return rc;
 projection_io=(m16_projection_io){(void *)(UINTPTR)0x80020000u,rd,wr,clean,invalidate,tick,barrier,0,60ULL*COUNTS_PER_SECOND};
 projection_io.arena_address=(uint32_t)(uintptr_t)projection_arena;
 rc=m16_projection_runtime_init(&p->projection,&projection_io,projection_arena,40000u,M16_PROJECTION_BUILD_ABI);if(rc)return rc;
 rc=m16_geometry_cache_init(&geometry,geometry_values,M16_GC_WORDS);if(rc)return rc;
 cfg=*provided;cfg.storage[0]=capture0;cfg.storage[1]=capture1;cfg.storage[2]=capture2;
 for(unsigned i=0;i<3;i++)cfg.physical[i]=(uint32_t)(uintptr_t)cfg.storage[i];
 rc=mic16_stream_init(&p->scheduler,&schedule,1,tick(0));if(rc)return rc;
 rc=m16_window_capture_arm_attach(&p->capture_port,&p->capture,&cfg);if(rc)return rc;
 /* DMA中断注册完成后永久锁住本平台实例；初始化失败也不能再次清对象掩盖所有权。 */
 attached=1;p->initialized=1;p->reference=reference;p->key=key;p->boot=boot;
#if M16_CAPTURE_PACKET_SAMPLES == 40960u
 rc=m16_capture_stream_init(&p->capture_stream,&p->capture,&p->scheduler);if(rc)return rc;
#endif
 rc=m16_output_pool_init(&p->output,&p->scheduler,output_storage,M16_OUTPUT_POOL_FLOATS);if(rc)return rc;
 return 0;
}
int m16_audio_platform_arm_step(m16_audio_platform_arm *p){
 m16_audio_resident_io io={0,send_packet};const m16_network_config network=M16_NETWORK_SITE_CONFIG;int rc;
 if(!p||!p->initialized)return -1300;
 if(p->error)return p->error;
 if(p->ready)return m16_audio_resident_step(&p->resident);
 /* 粗扫完成后进入IDLE，几何表仍要跨多步构建；不能再次poll已就绪粗扫。 */
 if(!m16_coarse_scan_available(&coarse)){
  rc=m16_coarse_scan_poll(&coarse);if(rc<0)return p->error=rc;
  if(rc)return 1;
 }
 rc=m16_geometry_cache_step(&geometry);if(rc<0)return p->error=rc;if(rc)return 1;
 mic16_spectral_init(&detection_work.extended.physical.spectral);
 rc=m16_detection_init(&p->detection,&detection_work,&coarse,&p->fft_task,&p->projection,&statistics);if(rc)return p->error=rc;
 rc=m16_physical_projection_bind_rows(&p->detection.front.refinement.projection,m16_geometry_cache_row,&geometry);if(rc)return p->error=rc;
 rc=m16_enh_prediction_init(&p->enhancement,&detection_work.extended,&enhancement_work);if(rc)return p->error=rc;
 rc=m16_continuous_task_init(&p->continuous,&continuous_work,1);if(rc)return p->error=rc;
 rc=m16_source_audio_init(&p->source_audio,&p->enhancement,&p->continuous,p->key);if(rc)return p->error=rc;
 rc=m16_audio_pipeline_init(&p->pipeline,&p->detection,&p->source_audio,&observation);if(rc)return p->error=rc;
#if M16_CAPTURE_PACKET_SAMPLES == 2048u
 mic16_spectral_init(&front_workspace);
 rc=m16_front_ring_init(&front_ring,&front_storage,&front_workspace);if(rc)return p->error=rc;
#if M16_PROJECTION_VERSION == 5u || M16_PROJECTION_VERSION == 6u
 rc=m16_front_ring_bind_evidence(&front_ring,&coarse);if(rc)return p->error=rc;
 rc=m16_spatial_front_bind_evidence(&p->detection.front,&front_ring.evidence.view);if(rc)return p->error=rc;
#endif
 rc=m16_front_context_init(&front_context,&front_ring,&p->detection,context_pcm,40960u*16u);if(rc)return p->error=rc;
 rc=m16_frame_front_stream_init(&front_stream,&p->capture,&front_ring);if(rc)return p->error=rc;
#if defined(M16_LOCALIZATION_CAPTURE_ONCE) && M16_LOCALIZATION_CAPTURE_ONCE
 rc=m16_frame_front_stream_set_capture_once(&front_stream,1u);if(rc)return p->error=rc;
#endif
 rc=m16_capture_stream_init_front(&p->capture_stream,&front_stream,&front_context,&p->scheduler);if(rc)return p->error=rc;
#endif
#if M16_PROJECTION_VERSION == 5u || M16_PROJECTION_VERSION == 6u
#if M16_CAPTURE_PACKET_SAMPLES != 2048u
#error V5和V6常驻平台必须使用2048子帧采集
#endif
#if M16_PROJECTION_VERSION == 6u
 rc=m16_shared_compute_graph_init(&p->shared_graph,&p->projection,&p->detection,&p->enhancement,&p->continuous,&front_ring);if(rc)return p->error=rc;
#else
 rc=m16_fft64_graph_init(&p->fft64_graph,&p->projection,&p->detection,&p->enhancement,&p->continuous,&front_ring);if(rc)return p->error=rc;
#endif
#endif
#if defined(M16_LOCALIZATION_ONLY) && M16_LOCALIZATION_ONLY
 rc=m16_source_audio_set_localization_only(&p->source_audio,1u);if(rc)return p->error=rc;
 rc=m16_output_pool_set_localization_only(&p->output,1u);if(rc)return p->error=rc;
#if defined(M16_LOCALIZATION_CAPTURE_ONCE) && M16_LOCALIZATION_CAPTURE_ONCE
 rc=m16_control_init(&p->control,p->boot,M16_CTL_SINGLE_CAPTURE|M16_CTL_LOCALIZATION|M16_CTL_RAW_PCM|M16_CTL_TELEMETRY);if(rc)return p->error=rc;
#else
 rc=m16_control_init(&p->control,p->boot,M16_CTL_CONTINUOUS|M16_CTL_LOCALIZATION|M16_CTL_RAW_PCM|M16_CTL_TELEMETRY);if(rc)return p->error=rc;
#endif
#else
 rc=m16_control_init(&p->control,p->boot,M16_CTL_CONTINUOUS|M16_CTL_SOURCE_AUDIO|M16_CTL_TELEMETRY);if(rc)return p->error=rc;
#endif
 rc=m16_audio_resident_init(&p->resident,&p->capture_stream,&p->pipeline,&p->output,&p->control,p->reference,p->key,&io);if(rc)return p->error=rc;
#if M16_PROJECTION_VERSION == 6u
 rc=m16_audio_resident_bind_backend(&p->resident,&p->shared_graph,sizeof(p->shared_graph),m16_shared_compute_graph_service);if(rc)return p->error=rc;
#elif M16_PROJECTION_VERSION == 5u
 rc=m16_audio_resident_bind_backend(&p->resident,&p->fft64_graph,sizeof(p->fft64_graph),m16_fft64_graph_service);if(rc)return p->error=rc;
#endif
 rc=m16_control_udp_open(&p->control_udp,&p->control,network.peer);if(rc)return p->error=rc;
 p->ready=1;return 0;
}
