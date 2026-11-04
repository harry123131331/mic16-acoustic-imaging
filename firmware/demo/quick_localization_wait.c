extern const unsigned char _binary_saved_environment_reference_bin_start[];
/* 有限三窗真实输入功能试验；名义通道映射，非连续实时或标定验收。仅延长人工准备时限，不改变检测/增强。 */
#include "../modules/mic16_cooperate.h"
/* 完整声像集成候选，必须搭配本提交包中的硬件文件。
 * 视频独立运行；音频使用有限采集、处理、逐源发送流程，输入未配置时停止。
 * 真实声像标定尚未验收，禁止与旧纯音频BIT混用。
 */
#include <stdio.h>
#include <string.h>
#include "xaxidma.h"
#include "xaxidma_hw.h"
#include "xuartps_hw.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xtime_l.h"
#include "mic16_capture.h"
#include "mic16_enhancement.h"
#if defined(MIC16_PREDICTION_ONLY) && MIC16_PREDICTION_ONLY
#include "mic16_enh_prediction.h"
#endif
#include "mic16_continuous.h"
#include "mic16_source_wire.h"
#include "mic16_network.h"
#include "mic16_network_site.h"
#include "mic16_directions_wire.h"
#include "mic16_heatmap_wire.h"
#include "mic16_live_bridge.h"
#include "mic16_input_guard.h"
#include "mic16_capture_session.h"
#include "mic16_site_config.h"
#include "mic16_av_policy.h"
#include "mic16_av_site.h"
#include "acoustic_projection_policy.h"
#include "acoustic_site_policy.h"
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
#include "mic16_av_runtime_arm.h"
#define SEND_AUDIO m16_runtime_arm_audio_send
#else
#define SEND_AUDIO m16_network_send
#endif
#if XPAR_CAMERA_BASEADDR != M16_AV_CONTROL_BASE
#error AV software requires the matching revision-7 video control address
#endif
#if (defined(MIC16_STREAM_REPLAY) && MIC16_STREAM_REPLAY) || (defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ)
#include "mic16_stream_runner.h"
static int stream_datagram(void *,const unsigned char *,unsigned);
#define OUTPUT_DATAGRAM stream_datagram
static mic16_stream_runner stream_runner;
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
#include "mic16_irq_stream.h"
#include "mic16_cache_slices.h"
#include "mic16_irq_capture_arm.h"
static mic16_irq_stream stream_bridge;
static mic16_irq_capture_arm stream_irq_capture __attribute__((aligned(64)));
static unsigned stream_current_input;
#endif
#else
#define OUTPUT_DATAGRAM SEND_AUDIO
#endif
#define GUARD_WORDS 16u
#define GUARD 0x75c3a91du
#if XPAR_AXI_DMA_0_INCLUDE_SG != 0 || XPAR_AXI_DMA_0_INCLUDE_S2MM_DRE != 0
#error AV2 needs frozen simple S2MM/no-DRE
#endif
typedef char expected_three_windows[(MIC16_BURST_WINDOWS==3u)?1:-1];
typedef char expected_raw_extent[(M16_SESSION_TOTAL_PACKETS*1024u==8388608u)?1:-1];
typedef struct {u32 before[16],payload[M16_RX_CAPACITY/4],after[16];} guarded_rx;
static guarded_rx buffer __attribute__((aligned(64)));
/* 只在有界内存中归档，不写入SD卡或电脑磁盘；采集中不进行大块内存复制。 */
static unsigned char raw_packets[M16_SESSION_TOTAL_PACKETS][1024] __attribute__((aligned(64)));
static float quiet[8192*16] __attribute__((aligned(64)));
static float pcm[40960*16] __attribute__((aligned(64)));
static mic16_pipeline_workspace workspace;
static mic16_pipeline_observation observation;
static mic16_search_memory memory;
static mic16_cycle_output event;
static mic16_live_input input;
static mic16_live_binding binding;
static mic16_capture_session session;
static XAxiDma dma;
static mic16_rx_state rx_state;
static unsigned int source_running;
static int packet_command,session_rc;
/* 使用volatile读取，防止未启用的现场配置被编译器优化掉整条代码路径。此项不是运行时标定接口；应修改头文件后重新构建，不能靠改写DDR绕过配置。 */
static volatile unsigned int site_input_verified=MIC16_SITE_INPUT_VERIFIED;
typedef struct {
    u32 signature,version,stage,code,quarantined,final_reset_ok,computed;
    u32 reference_packets,audio_packets,discarded,warmup,received,early_start,audio_first_sequence;
    u32 continuity_physically_verified;
    u64 timer_hz,compute_ticks[3],audio_end_ticks[3];
    mic16_rx_report last_packet;
    mic16_cycle_output cycles[3];
} combined_result;
volatile combined_result g_mic16_av2_result;
static void snapshot(void *ctx,mic16_rx_regs *r) {
    UINTPTR b=XPAR_AXI_DMA_0_BASEADDR+XAXIDMA_RX_OFFSET;(void)ctx;
    r->sr=XAxiDma_ReadReg(b,XAXIDMA_SR_OFFSET);r->cr=XAxiDma_ReadReg(b,XAXIDMA_CR_OFFSET);
    r->destination=XAxiDma_ReadReg(b,XAXIDMA_DESTADDR_OFFSET);r->length=XAxiDma_ReadReg(b,XAXIDMA_BUFFLEN_OFFSET);
}
static unsigned long long ticks(void *ctx){XTime t;(void)ctx;XTime_GetTime(&t);return t;}
/* 计时只写诊断记录，不修改冻结检测器及音频时间合同。 */
volatile mic16_pipeline_profile g_mic16_pipeline_profiles[3];
volatile u64 g_mic16_output_stage_ticks[3][3];
#if MIC16_INPUT_GUARD_ENABLED
volatile mic16_input_report g_mic16_input_guard_report;
#endif
static mic16_pipeline_profile current_pipeline_profile;
static const mic16_profile_clock pipeline_clock={0,ticks};
static int profiled_observer(const float *pcm_input,const float *reference,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w,void *context){
    return mic16_pipeline_observe_timed(pcm_input,reference,o,w,&pipeline_clock,
        (mic16_pipeline_profile *)context);
}
static void stop_source(void *ctx){(void)ctx;Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR,0);source_running=0;}
static int reset_checked(void *ctx) {
    XTime b,n;(void)ctx;XAxiDma_Reset(&dma);XTime_GetTime(&b);
    do {if(XAxiDma_ResetIsDone(&dma))return 1;XTime_GetTime(&n);}while(n-b<COUNTS_PER_SECOND/10u);
    return 0;
}
static void prepare(void *ctx) {
    unsigned int i;(void)ctx;
    for(i=0;i<16;i++){buffer.before[i]=GUARD;buffer.after[i]=GUARD;}
    for(i=0;i<M16_RX_CAPACITY/4;i++)buffer.payload[i]=0xa5a5a5a5u;
    Xil_DCacheFlushRange((INTPTR)&buffer,sizeof(buffer));
}
static int submit(void *ctx,unsigned int bytes) {
    (void)ctx;if(!m16_av_audio_dma_destination((uint32_t)(UINTPTR)buffer.payload,bytes))return XST_FAILURE;
    XAxiDma_IntrAckIrq(&dma,XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);
    return XAxiDma_SimpleTransfer(&dma,(UINTPTR)buffer.payload,bytes,XAXIDMA_DEVICE_TO_DMA);
}
static void start_source(void *ctx){(void)ctx;if(!source_running){Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR,1);source_running=1;}}
static void ack(void *ctx){(void)ctx;XAxiDma_IntrAckIrq(&dma,XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);}
static int consume(void *ctx,unsigned int bytes) {
    unsigned int i,slot;(void)ctx;
    Xil_DCacheInvalidateRange((INTPTR)&buffer,sizeof(buffer));
    for(i=0;i<16;i++)if(buffer.before[i]!=GUARD || buffer.after[i]!=GUARD)return 0;
    if(bytes!=1024)return 0;
    session_rc=mic16_session_packet(&session,packet_command,&slot);
    if(session_rc>0) {
        memcpy(raw_packets[slot],buffer.payload,1024);
        if(slot>=512u && (slot-511u)%2560u==0u)
            g_mic16_av2_result.audio_end_ticks[(slot-512u)/2560u]=ticks(0);
    } /* 这里只记录CPU看到数据包到达的时间，尚未验证物理采样时间戳。 */
    return 1; /* 用户主动发送X取消命令不属于DMA保护失败。 */
}
static int command_now(void) {
    int c,chosen=0;unsigned int budget=64;
    while(budget-- && XUartPs_IsReceiveData(STDIN_BASEADDRESS)) {
        c=XUartPs_ReadReg(STDIN_BASEADDRESS,XUARTPS_FIFO_OFFSET)&255;
        if(c=='X'||c=='x')chosen='X';
        else if((c=='A'||c=='a'||c=='R'||c=='r') && chosen!='X')chosen=c;
    }
    return chosen;
}
/* 无新增背景采集；用户先对焦，明确开始播放后再发A。 */
static int wait_reference_request(void) {
 printf("QUICK_READY_WAIT_A VIDEO_ONLY_UNTIL_EXPLICIT_A NO_BACKGROUND_CAPTURE\r\n");
 for(;;){int c;M16_COOPERATE();c=command_now();if(c=='A'||c=='a')return 0;if(c=='X'||c=='x')return -1;}
}
static const m16_av_calibration av_calibration=MIC16_AV_CALIBRATION_INITIALIZER;
static const acoustic_projection_policy projection_policy=ACOUSTIC_PROJECTION_POLICY_INITIALIZER;
typedef struct {
    u32 id,version,video[8],config_status,snapshot_number,omitted;
    int submission,reason;
    u64 observed_audio_end_ms,submission_ms;
} av2_video_result;
volatile av2_video_result g_mic16_av2_video;
static unsigned int av_device_present;
static uint32_t av_read(void *ctx,uint32_t offset) {
    __asm__ volatile("dmb sy" ::: "memory");
    return Xil_In32((UINTPTR)ctx+offset);
}
static void av_write(void *ctx,uint32_t offset,uint32_t value) {
    __asm__ volatile("dmb sy" ::: "memory");
    Xil_Out32((UINTPTR)ctx+offset,value);
    __asm__ volatile("dmb sy" ::: "memory");
}
static u64 milliseconds(u64 tick) {
    return (tick/COUNTS_PER_SECOND)*1000ULL+(tick%COUNTS_PER_SECOND)*1000ULL/COUNTS_PER_SECOND;
}
static void av_diagnostics(void) {
    unsigned int i;
    if(!av_device_present)return;
    for(i=0;i<8;i++)g_mic16_av2_video.video[i]=av_read((void *)M16_AV_CONTROL_BASE,0x80u+4u*i);
    g_mic16_av2_video.config_status=av_read((void *)M16_AV_CONTROL_BASE,0xa0u);
    printf("AV2_CONFIG revision=%lu camera_ready=%lu hdmi_ready=%lu raw=%08lx\r\n",
        (unsigned long)(g_mic16_av2_video.config_status>>16),
        (unsigned long)(g_mic16_av2_video.config_status&1u),
        (unsigned long)((g_mic16_av2_video.config_status>>1)&1u),
        (unsigned long)g_mic16_av2_video.config_status);
    g_mic16_av2_video.snapshot_number++;
    printf("AV2_VIDEO status=%08lx complete=%lu reject=%lu repeat=%lu supersede=%lu underflow=%lu axi_errors=%lu seq=%lu\r\n",
        (unsigned long)g_mic16_av2_video.video[0],(unsigned long)g_mic16_av2_video.video[1],
        (unsigned long)g_mic16_av2_video.video[2],(unsigned long)g_mic16_av2_video.video[3],
        (unsigned long)g_mic16_av2_video.video[4],(unsigned long)g_mic16_av2_video.video[5],
        (unsigned long)g_mic16_av2_video.video[6],(unsigned long)g_mic16_av2_video.video[7]);
    /* 各字分别同步，是诊断快照，并不构成原子的视频帧记录。 */
}
static void av_publish(unsigned int cycle) {
    int reason,rc;u32 omitted;acoustic_projection_report projection_report;u64 now=ticks(0);
    if(!av_device_present)return;
    g_mic16_av2_video.observed_audio_end_ms=milliseconds(g_mic16_av2_result.audio_end_ticks[cycle]);
    g_mic16_av2_video.submission_ms=milliseconds(now);
    rc=acoustic_publish_overlay(&av_calibration,&projection_policy,&event,MIC16_AV_MOUNT_ID,MIC16_AV_MODE_ID,
        MIC16_AV_AUDIO_TIMESTAMP_VERIFIED,g_mic16_av2_video.observed_audio_end_ms,
        g_mic16_av2_video.submission_ms,MIC16_AV_MAX_AGE_MS,(void *)M16_AV_CONTROL_BASE,
        av_read,av_write,&reason,&projection_report);
    omitted=projection_report.omitted;
    printf("AV_PROJECTION policy=%lu mode=%lu approximate=%lu\r\n",(unsigned long)projection_report.policy_id,(unsigned long)projection_report.mode,(unsigned long)projection_report.approximate_points);
    g_mic16_av2_video.submission=rc;g_mic16_av2_video.reason=reason;g_mic16_av2_video.omitted=omitted;
    printf("AV2_OVERLAY cycle=%u submit=%d reason=%d omitted=%lu timestamp_verified=%u calibration=%lu\r\n",
        cycle,rc,reason,(unsigned long)omitted,MIC16_AV_AUDIO_TIMESTAMP_VERIFIED,(unsigned long)av_calibration.calibrated);
    /* 不忙等，也不排队重放过期结果。硬件有效期和下一份新结果决定标记是否显示；驻留循环不得刷新旧结果。 */
}
static void av_initialize(void) {
    g_mic16_av2_video.id=av_read((void *)M16_AV_CONTROL_BASE,0);
    g_mic16_av2_video.version=av_read((void *)M16_AV_CONTROL_BASE,4);
    av_device_present=g_mic16_av2_video.id==M16_AV_MAGIC && g_mic16_av2_video.version==M16_AV_VERSION;
    printf("AV2_VIDEO_DEVICE id=%08lx version=%08lx present=%u; overlay requires real calibration + verified timestamps\r\n",
        (unsigned long)g_mic16_av2_video.id,(unsigned long)g_mic16_av2_video.version,av_device_present);
    av_publish(0);av_diagnostics();
}
/* 有限三窗CPU处理候选。各声源及跨窗音频边界分别存放在DDR；整窗输出成功后才能提交声源槽位。本版本接入UDP发送，但不代表持续实时处理已通过。 */
static m16_enh_workspace enhancement_workspace;
static m16_cont_workspace continuous_workspace;
static m16_cont_prediction current_prediction;
static float continuous_audio[3][24][2][16778] __attribute__((aligned(64)));
typedef struct {
    u32 signature,version,committed[3],counts[3],source_ids[3][24],current[3][24],samples[3];
    u32 wire_packets[3],wire_bytes[3],wire_digest[3];
    u64 output_start[3],output_end[3],compute_ticks[3];
    int code[3];
} continuous_result;
volatile continuous_result g_mic16_continuous_result;
static unsigned wire_window;
static int source_datagram_receipt(void *context,const unsigned char *data,unsigned length){
    unsigned i;(void)context;
    if(wire_window>=3 || length>1084 || length<60)return -1;
    /* 先记录编码摘要，再交给实际网络发送接口；返回值只表示本地发送处理结果。 */
    g_mic16_continuous_result.wire_packets[wire_window]++;
    g_mic16_continuous_result.wire_bytes[wire_window]+=length;
    for(i=0;i<length;i++)g_mic16_continuous_result.wire_digest[wire_window]=
        g_mic16_continuous_result.wire_digest[wire_window]*16777619u^data[i];
    return OUTPUT_DATAGRAM(0,data,length);
}
static int continuous_sink(void *context,const m16_cont_info *info,const double *a,const double *b,unsigned samples){
    unsigned i,c=info->window,s=info->ordinal;(void)context;
    if(c>=3 || info->stream_count>24 || samples>16778)return -1;
    if(info->source_id){
        if(s>=24)return -1;
        for(i=0;i<samples;i++){continuous_audio[c][s][0][i]=(float)a[i];continuous_audio[c][s][1][i]=(float)b[i];}
        g_mic16_continuous_result.source_ids[c][s]=info->source_id;
        g_mic16_continuous_result.current[c][s]=info->current_confirmed;
    }
    g_mic16_continuous_result.counts[c]=info->stream_count;
    g_mic16_continuous_result.samples[c]=samples;
    g_mic16_continuous_result.output_start[c]=info->output_start;
    g_mic16_continuous_result.output_end[c]=info->output_end;
    wire_window=c;
    return m16_source_encode(info,a,b,samples,source_datagram_receipt,0)<0;
}
#if defined(MIC16_PREDICTION_ONLY) && MIC16_PREDICTION_ONLY
static int collect_prediction(void *context,unsigned id,unsigned candidate,const float *mask){
    unsigned j,n=current_prediction.count;(void)context;
    if(n>=8 || candidate>=16)return -1;
#else
static int collect_prediction(void *context,unsigned id,unsigned candidate,const float *features,const float *mask,
    const double *spectrum,const double *wdas,const double *enhanced,unsigned samples){
    unsigned j,n=current_prediction.count;(void)context;(void)features;(void)spectrum;(void)wdas;(void)enhanced;
    if(n>=8 || candidate>=16 || samples!=16778)return -1;
#endif
    current_prediction.ids[n]=id;
    current_prediction.directions[n][0]=observation.directions[candidate*2];
    current_prediction.directions[n][1]=observation.directions[candidate*2+1];
    for(j=0;j<67*64;j++)current_prediction.masks[n][j]=mask[j];
    current_prediction.count++;return 0;
}
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
static void stream_cache_flush(void *p,uintptr_t a,unsigned n){(void)p;Xil_DCacheFlushRange((INTPTR)a,n);}
static int stream_cache_checkpoint(void *p){(void)p;M16_COOPERATE();return stream_runner.queue.failure? -1:0;}
#endif
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
static int commit_continuous_output(unsigned output){
#else
static void commit_continuous_output(unsigned output){
#endif
    unsigned count=g_mic16_continuous_result.counts[output];
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    if(count && mic16_cache_flush_slices((uintptr_t)continuous_audio[output],count*2u*16778u*4u,stream_cache_flush,stream_cache_checkpoint,0))return -1;
#else
    if(count)Xil_DCacheFlushRange((INTPTR)continuous_audio[output],count*2u*16778u*4u);
#endif
    __asm__ volatile("dmb sy" ::: "memory");
    g_mic16_continuous_result.committed[output]=1;
    Xil_DCacheFlushRange((INTPTR)&g_mic16_continuous_result,sizeof(g_mic16_continuous_result));
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    return 0;
#endif
}
/* 展示独立UDP5003，最多两包，不等待ARP或包间隔，不中断原方向和音频。 */
static m16_heatmap_workspace heatmap_workspace;
volatile unsigned g_mic16_heatmap_windows,g_mic16_heatmap_dropped;
static int heatmap_sink(void *unused,const unsigned char *data,unsigned bytes){
    (void)unused;return m16_network_try_send_to(5003,data,bytes);
}
static void publish_heatmap(unsigned cycle){
    int rc=m16_heatmap_encode(20261019u,cycle,workspace.maps,&heatmap_workspace,heatmap_sink,0);
    if(rc==2)g_mic16_heatmap_windows++;else g_mic16_heatmap_dropped++;
    printf("AV_HEATMAP cycle=%u rc=%d independent_display_NOT_source_confirmation\r\n",cycle,rc);
}
static int advance_continuous(unsigned cycle){
    int count,code;unsigned i,previous;XTime begin,end,direction_start,direction_end,enh_end;
    if(cycle>=3)return -1;
    if(!cycle){
        g_mic16_continuous_result.signature=0x4d434f4eu;
        g_mic16_continuous_result.version=0x20261019u;
        if(m16_cont_init(&continuous_workspace,20261019u))return -1;
    }
    XTime_GetTime(&direction_start);
    if(m16_directions_encode(20261019u,cycle,&event,OUTPUT_DATAGRAM,0)<0)return -4;
    XTime_GetTime(&direction_end);
    XTime_GetTime(&begin);current_prediction.count=0;
#if defined(MIC16_PREDICTION_ONLY) && MIC16_PREDICTION_ONLY
    count=m16_enh_predict_window(pcm,quiet,observation.directions,observation.valid,&event,&workspace.extended,
        &enhancement_workspace,collect_prediction,0);
#else
    count=m16_enh_window(pcm,quiet,observation.directions,observation.valid,&event,&workspace.extended,
        &enhancement_workspace,collect_prediction,0);
#endif
    XTime_GetTime(&enh_end);
    if(count<0 || (unsigned)count!=current_prediction.count)return -2;
    if(count)for(i=0;i<320;i++)current_prediction.weights[i]=enhancement_workspace.weights[i];
    previous=continuous_workspace.emitted;
    code=m16_cont_push(&continuous_workspace,pcm,&current_prediction,(u64)cycle*40960,continuous_sink,0);
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    if(code>=0 && continuous_workspace.emitted>previous && commit_continuous_output(previous))return -220;
#else
    if(code>=0 && continuous_workspace.emitted>previous)commit_continuous_output(previous);
#endif
    XTime_GetTime(&end);g_mic16_continuous_result.compute_ticks[cycle]=end-begin;
    g_mic16_output_stage_ticks[cycle][0]=direction_end-direction_start;
    g_mic16_output_stage_ticks[cycle][1]=enh_end-begin;
    g_mic16_output_stage_ticks[cycle][2]=end-enh_end;
    g_mic16_continuous_result.code[cycle]=code;
    if(code<0)return -3;
    printf("AV7_CONTINUOUS_INPUT cycle=%u current=%d emitted=%u UDP_CANDIDATE_NOT_BOARD_OR_REALTIME_ACCEPTANCE\r\n",
        cycle,count,(unsigned)continuous_workspace.emitted);
    return 0;
}
static int finish_continuous(void){
    unsigned previous=continuous_workspace.emitted;
    int code=m16_cont_finish(&continuous_workspace,continuous_sink,0);
    if(code<0 || previous>=3)return -1;
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    if(continuous_workspace.emitted>previous && commit_continuous_output(previous))return -220;
#else
    if(continuous_workspace.emitted>previous)commit_continuous_output(previous);
#endif
    printf("AV7_CONTINUOUS_FINISHED windows=%u samples=%lu submitted_to_lwIP_NOT_RECEIPT_OR_REALTIME\r\n",
        (unsigned)continuous_workspace.emitted,(unsigned long)g_mic16_continuous_result.output_end[2]);
    return 0;
}

static int process_replay_cycle(unsigned c){
    unsigned i,k;int rc;XTime a,b;
        for(k=0;k<2560;k++) {
            if((k&15u)==0)M16_COOPERATE();
            i=c*2560+k;
            {
                const unsigned char *raw=raw_packets[512+i];
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
                raw=mic16_irq_stream_packet(&stream_bridge,stream_current_input,c,k);
                if(!raw)return -214;
#endif
                rc=mic16_live_push(&input,raw,1024,session.audio_first_sequence+i,1);
            }
            if(rc!=(k==2559?M16_LIVE_WINDOW_READY:0))return -204;
        }
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
        /* 解码已复制到独立PCM；归还原始窗后，ISR可以复用它，算法不再读取原始包。 */
        if(mic16_irq_stream_release(&stream_bridge,stream_current_input))return -215;
#endif
        memset(&event,0,sizeof(event));XTime_GetTime(&a);
        memset(&current_pipeline_profile,0,sizeof(current_pipeline_profile));
        current_pipeline_profile.pipeline_rc=-99;
#if MIC16_INPUT_GUARD_ENABLED
        {
            mic16_guarded_observer_context guarded={profiled_observer,&current_pipeline_profile,{M16_INPUT_UNCHECKED,0,0,0,0xffffffffu,0xffffffffu,0}};
            rc=mic16_live_run_with_observer(&input,&binding,&memory,&observation,&workspace,&event,
                mic16_observe_with_input_guard,&guarded);
            g_mic16_input_guard_report=guarded.report;
            if(guarded.report.status<0){
                /* 检查未通过时不发布无源包或旧观测；当前会话停止且必须重新取得参考。 */
                current_pipeline_profile.pipeline_rc=guarded.report.status;
                mic16_live_interrupt(&input,1);
                printf("AV_INPUT_INVALID cycle=%u reason=%d checked=%u reference_checked=%u nonzero=%u zero_channels=%04x NOT_A_NORMAL_ZERO_SOURCE_RESULT\r\n",
                    c,guarded.report.status,guarded.report.observed_checked,guarded.report.reference_checked,
                    guarded.report.nonzero_observed,guarded.report.zero_channel_mask);
            }
            Xil_DCacheFlushRange((INTPTR)&g_mic16_input_guard_report,sizeof(g_mic16_input_guard_report));
        }
#else
        rc=mic16_live_run_with_observer(&input,&binding,&memory,&observation,&workspace,&event,
            profiled_observer,&current_pipeline_profile);
#endif
        XTime_GetTime(&b);g_mic16_av2_result.compute_ticks[c]=b-a;
        g_mic16_pipeline_profiles[c]=current_pipeline_profile;
        Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_profiles[c],sizeof(g_mic16_pipeline_profiles[c]));
        printf("AV_PROFILE_DETECT cycle=%u clock_ok=%u attempted=%u complete=%u rc=%d timer_hz=%llu ticks=",
            c,current_pipeline_profile.clock_ok,current_pipeline_profile.attempted_mask,
            current_pipeline_profile.completed_mask,current_pipeline_profile.pipeline_rc,
            (unsigned long long)COUNTS_PER_SECOND);
        for(i=0;i<MIC16_PROFILE_STAGES;i++)printf("%s%llu",i?",":"",current_pipeline_profile.ticks[i]);
        printf("\r\n");
        if(rc)return -205;
        g_mic16_av2_result.cycles[c]=event;g_mic16_av2_result.computed++;
        printf("AV2_CYCLE i=%u candidates=%u retained=%d confirmed=%d new=%d elapsed_ms=%lu\r\n",c,
            observation.candidate_count,event.count,event.confirmed_count,event.new_source_valid,
            (unsigned long)((b-a)*1000ULL/COUNTS_PER_SECOND));
        for(i=0;i<(unsigned int)event.count;i++)printf("AV2_SOURCE cycle=%u id=%u az=%.3f el=%.3f score=%.6f current=%d\r\n",c,
            event.tracks[i].source_id,event.tracks[i].azimuth,event.tracks[i].elevation,
            event.tracks[i].score,event.tracks[i].confirmed_this_cycle);
        publish_heatmap(c);
        if(m16_directions_encode(20261019u,c,&event,OUTPUT_DATAGRAM,0)<0)return -206;
        Xil_DCacheFlushRange((INTPTR)&g_mic16_output_stage_ticks[c],sizeof(g_mic16_output_stage_ticks[c]));
        printf("AV_PROFILE_OUTPUT cycle=%u direction_ticks=%llu enhancement_ticks=%llu reconstruct_udp_ticks=%llu\r\n",
            c,g_mic16_output_stage_ticks[c][0],g_mic16_output_stage_ticks[c][1],g_mic16_output_stage_ticks[c][2]);
        av_publish(c);av_diagnostics();
    
    return 0;
}

#if (defined(MIC16_STREAM_REPLAY) && MIC16_STREAM_REPLAY) || (defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ)
/* 此后端只引用先前归档的三窗，绝不声称计算期间DMA还在采集。
 * 真正连续采集须替换start/poll/stop并验证中断时限、缓存和物理溢出。
 * 报文暂存在独立DDR，整窗重建成功后才由运行器逐包发送。 */
#define STREAM_STAGED_BYTES (264u+24u*132u*(4u+1084u))
static unsigned char stream_staged_packets[3][STREAM_STAGED_BYTES] __attribute__((aligned(64)));
static unsigned stream_bytes[3],stream_offsets[3];
#if !defined(MIC16_STREAM_IRQ) || !MIC16_STREAM_IRQ
static unsigned stream_raw_window[3],stream_capture_active;
#endif

typedef struct {u32 signature,version,backend,stage;int code;u64 computed,emitted,sent;u32 peak_input,peak_output;} stream_run_result;
volatile stream_run_result g_mic16_stream_run;
static u32 stream_get32(const unsigned char *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static void stream_put32(unsigned char *p,u32 v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static int stream_datagram(void *p,const unsigned char *data,unsigned bytes){
    unsigned slot,offset;(void)p;
    if(!data || bytes<36 || bytes>1084 || mic16_stream_runner_build_slot(&stream_runner,stream_get32(data+12),&slot))return -1;
    offset=stream_bytes[slot];if(offset>STREAM_STAGED_BYTES-(bytes+4u))return -1;
    stream_put32(stream_staged_packets[slot]+offset,bytes);memcpy(stream_staged_packets[slot]+offset+4,data,bytes);
    stream_bytes[slot]=offset+bytes+4;return 0;
}
static int stream_capture_start(void *p,unsigned slot,const mic16_stream_window *w){
    (void)p;
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    return mic16_irq_stream_begin(&stream_bridge,slot,w);
#else
    u64 n=w->first_sample/40960;
    if(stream_capture_active || slot>=3 || n>=3 || session.stored!=8192 || source_running)return -1;
    stream_raw_window[slot]=(unsigned)n;stream_capture_active=1;return 0;
#endif
}
static int stream_capture_poll(void *p){(void)p;
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
 return mic16_irq_stream_poll(&stream_bridge);
#else
 if(!stream_capture_active)return -1;stream_capture_active=0;return 1;
#endif
}
static int stream_capture_stop(void *p){(void)p;
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
 return mic16_irq_stream_stop(&stream_bridge);
#else
 stream_capture_active=0;return source_running?0:1;
#endif
}
static int stream_compute(void *p,mic16_stream_runner *r,unsigned input_slot,
    const mic16_stream_window *w,unsigned output_slot,u64 *emitted){
    int rc;unsigned c=(unsigned)(w->first_sample/40960);(void)p;(void)r;
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    if(input_slot>=3 || !stream_bridge.held[input_slot] || stream_bridge.leases[input_slot].window!=c)return -1;
    stream_current_input=input_slot;
    g_mic16_av2_result.audio_end_ticks[c]=stream_bridge.leases[input_slot].received_tick;
#else
    if(input_slot>=3 || stream_raw_window[input_slot]!=c)return -1;
#endif
    stream_bytes[output_slot]=stream_offsets[output_slot]=0;
    rc=process_replay_cycle(c);*emitted=continuous_workspace.emitted;return rc;
}
static int stream_finish(void *p,mic16_stream_runner *r,u64 *emitted){
    int rc;(void)p;(void)r;rc=finish_continuous();*emitted=continuous_workspace.emitted;return rc;
}
static int stream_send_step(void *p,unsigned slot,const mic16_stream_window *w){
    unsigned bytes,offset;(void)p;if(slot>=3)return -1;offset=stream_offsets[slot];
    if(offset>=stream_bytes[slot])return -1;
    bytes=stream_get32(stream_staged_packets[slot]+offset);
    if(bytes<36 || bytes>1084 || offset+4u+bytes>stream_bytes[slot] ||
        stream_get32(stream_staged_packets[slot]+offset+16)!=w->first_sample/40960)return -1;
    if(SEND_AUDIO(0,stream_staged_packets[slot]+offset+4,bytes))return -1;
    stream_offsets[slot]=offset+4+bytes;
    if(stream_offsets[slot]==stream_bytes[slot]){stream_offsets[slot]=stream_bytes[slot]=0;return 1;}
    return 0;
}
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
static int irq_open(void *p){(void)p;return mic16_irq_capture_arm_open(&stream_irq_capture,(unsigned char *)raw_packets,3u*2560u*1024u,0);}
static int irq_status(void *p){(void)p;return mic16_irq_capture_arm_status(&stream_irq_capture);}
static int irq_take(void *p,mic16_irq_lease *l){(void)p;return mic16_irq_capture_arm_take(&stream_irq_capture,l);}
static const unsigned char *irq_packet(void *p,const mic16_irq_lease *l,unsigned k){(void)p;return mic16_irq_capture_arm_packet(&stream_irq_capture,l,k);}
static int irq_release(void *p,const mic16_irq_lease *l){(void)p;return mic16_irq_capture_arm_release(&stream_irq_capture,l);}
static int irq_stop(void *p){(void)p;return mic16_irq_capture_arm_stop(&stream_irq_capture);}
static void stream_video_service(void *p){(void)p;m16_runtime_arm_service();}
static void stream_irq_service(void *p){
    int rc;(void)p;
    rc=mic16_irq_stream_status(&stream_bridge);
    if(rc<0 && !stream_runner.queue.failure)(void)mic16_stream_runner_stop(&stream_runner,M16_STREAM_INPUT_INVALID);
    if(!stream_runner.queue.failure)(void)mic16_stream_runner_checkpoint(&stream_runner);
    m16_runtime_arm_service();
}
#endif
static int run_stream_replay(void){
    mic16_stream_runner_config c={{3,3,COUNTS_PER_SECOND,60ULL*COUNTS_PER_SECOND,
        60ULL*COUNTS_PER_SECOND,MIC16_SITE_POSE_REFERENCE_ID,MIC16_SITE_POSE_REFERENCE_ID},COUNTS_PER_SECOND,3};
    mic16_stream_runner_ops ops={0,ticks,stream_capture_start,stream_capture_poll,stream_capture_stop,
        stream_compute,stream_finish,stream_send_step};int rc;
    g_mic16_stream_run.signature=0x4d535252;g_mic16_stream_run.version=20261021;
    g_mic16_stream_run.backend=1;g_mic16_stream_run.stage=1;
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    {
      mic16_irq_stream_ops backend={0,irq_open,irq_status,irq_take,irq_packet,irq_release,irq_stop};
      if(mic16_irq_stream_init(&stream_bridge,&backend))return -216;
    }
    g_mic16_stream_run.backend=2;
    c.capture_timeout_ticks=2ULL*COUNTS_PER_SECOND;
    printf("STREAM_RUNNER backend=LIVE_IRQ bounded_model_windows=3 source_limit=0 realtime_accepted=0\r\n");
#else
    printf("STREAM_RUNNER backend=ARCHIVED_THREE_WINDOWS asynchronous_DMA=0 realtime_accepted=0\r\n");
#endif
    rc=mic16_stream_runner_init(&stream_runner,&c,&ops,20261019);
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    if(!rc && m16_cooperate_bind(stream_irq_service,0))rc=-217;
#endif
    while(!rc){M16_COOPERATE();rc=mic16_stream_runner_step(&stream_runner);}
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    {
      u64 stop_start=ticks(0);int closed=0;
      (void)m16_cooperate_bind(stream_video_service,0);
      /* 回调已经返回后才能回收原始窗租约；复位超时则隔离，不能再重用。 */
      do{closed=mic16_irq_stream_close(&stream_bridge);if(closed)break;M16_COOPERATE();}
      while(ticks(0)-stop_start<COUNTS_PER_SECOND/5u);
      if(closed && stream_runner.queue.failure){
        /* 设备复位确认后，再归还运行器元数据中的最后采集/计算租约。 */
        (void)mic16_stream_runner_stop(&stream_runner,stream_runner.queue.failure);
        if(rc==M16_RUNNER_QUIESCING)rc=stream_runner.queue.failure;
      }
      if(!closed)rc=-218;
      printf("STREAM_IRQ_STOP raw_failure=%d irq_failure=%d packets=%llu stopped=%u closed=%d\r\n",stream_bridge.failure,stream_irq_capture.ring.failure,stream_irq_capture.ring.packets,stream_irq_capture.stopped,closed);
    }
#endif
    g_mic16_stream_run.computed=stream_runner.computed;g_mic16_stream_run.emitted=stream_runner.emitted;
    g_mic16_stream_run.sent=stream_runner.queue.stats.outputs_sent;
    g_mic16_stream_run.peak_input=stream_runner.queue.stats.peak_input_slots;
    g_mic16_stream_run.peak_output=stream_runner.queue.stats.peak_output_slots;
    g_mic16_stream_run.code=rc;g_mic16_stream_run.stage=rc==M16_RUNNER_DONE?2:3;
    Xil_DCacheFlushRange((INTPTR)&g_mic16_stream_run,sizeof(g_mic16_stream_run));
    printf("STREAM_RUNNER_END rc=%d computed=%llu emitted=%llu sent=%llu NOT_SUSTAINED_REALTIME_ACCEPTANCE\r\n",
        rc,stream_runner.computed,stream_runner.emitted,stream_runner.queue.stats.outputs_sent);
    return rc==M16_RUNNER_DONE?0:rc;
}
#endif
static int replay(const mic16_live_config *config) {
    unsigned int i;int rc;
#if !((defined(MIC16_STREAM_REPLAY) && MIC16_STREAM_REPLAY) || (defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ))
    unsigned c;
#endif
    rc=mic16_live_init(&input,config,quiet,8192*16,pcm,40960*16);if(rc)return -200;
    mic16_memory_init(&memory);
    if(mic16_live_begin_reference(&input))return -201;
    for(i=0;i<512;i++) {
        if((i&15u)==0)M16_COOPERATE();
        rc=mic16_live_push(&input,raw_packets[i],1024,i,1);
        if(rc!=(i==511?M16_LIVE_REFERENCE_READY:0))return -202;
    }
    if(mic16_live_begin_audio(&input))return -203;
    {
        /* 默认关闭现场开关；试验前必须核对实际电脑、网卡及现场参数。 */
        const m16_network_config network=M16_NETWORK_SITE_CONFIG;
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
        (void)network;
        if(m16_runtime_arm_network())return -208;
#else
        if(m16_network_init(&network))return -208;
#endif
    }
#if (defined(MIC16_STREAM_REPLAY) && MIC16_STREAM_REPLAY) || (defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ)
    return run_stream_replay();
#else
    for(c=0;c<1;c++)if((rc=process_replay_cycle(c)))return rc;

    /* 本次只验定位/显示，正式逐源增强保留在完整固件。 */
    return 0;
#endif
}
int main(void) {
    mic16_live_config config={MIC16_SITE_SCALE_EXPONENT,MIC16_SITE_LOGICAL_TO_WIRE,MIC16_SITE_POSE_REFERENCE_ID};
    mic16_rx_ops ops={0,prepare,submit,start_source,snapshot,ticks,stop_source,reset_checked,consume,ack};
    mic16_rx_report r;XAxiDma_Config *cfg;unsigned int i;XTime begin,now;int rc=0;
    Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
    g_mic16_av2_result.signature=0x4d415631;g_mic16_av2_result.version=0x20261018;
    g_mic16_av2_result.stage=1;g_mic16_av2_result.timer_hz=COUNTS_PER_SECOND;
    #if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    printf("\r\nMIC16 LIVE IRQ FULL PIPELINE BOUNDED TRIAL GATED NOT REALTIME\r\n");
    #elif defined(MIC16_STREAM_REPLAY) && MIC16_STREAM_REPLAY
    printf("\r\nMIC16 STREAM RUNNER ARCHIVED INPUT CANDIDATE GATED NOT CONTINUOUS DMA\r\n");
#else
    printf("\r\nMIC16 AV7 FINITE NETWORK REPLAY CANDIDATE GATED NOT BOARD ACCEPTED\r\n");
#endif
    av_initialize();
    printf("QUICK_LOCALIZATION_ONLY SAVED_ENVIRONMENT_REFERENCE NO_NEW_BACKGROUND NO_ENHANCEMENT_TEST\r\n");
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
    if(m16_runtime_arm_start()) {rc=-116;goto publish;}
#endif
    printf("checkpoint=%s result=0x%08lx\r\n",mic16_frozen_checkpoint_sha256(),(unsigned long)&g_mic16_av2_result);
    if(!site_input_verified || config.scale_exponent>31u) {
        printf("MIC16_AV2_NEEDS_INPUT_CONFIG no_capture: measure scale/map with D4G, edit mic16_site_config.h, rebuild\r\n");
        g_mic16_av2_result.stage=4;goto parked;
    }
    for(i=0;i<16;i++) {
        unsigned int j;if(config.logical_to_wire[i]>15u){rc=-90;goto publish;}
        for(j=0;j<i;j++)if(config.logical_to_wire[i]==config.logical_to_wire[j]){rc=-91;goto publish;}
    }
    Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR+4,0);Xil_Out32(XPAR_AXI_GPIO_1_BASEADDR+4,0);
    stop_source(0);Xil_Out32(XPAR_AXI_GPIO_1_BASEADDR,20);
    cfg=XAxiDma_LookupConfig(XPAR_AXI_DMA_0_DEVICE_ID);
    if(!cfg || XAxiDma_CfgInitialize(&dma,cfg)!=XST_SUCCESS || XAxiDma_HasSg(&dma)){rc=-100;goto publish;}
    XAxiDma_IntrDisable(&dma,XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);
    if(!reset_checked(0)){rx_state.quarantined=1;rc=-101;goto publish;}
    printf("115200 8N1. Saved environment reference; no new background capture.\r\n");
    printf("Adjust focus first; wait for explicit playback instruction, then A. X cancels.\r\n");
    printf("No UART output during capture; waiting packets are consumed/discarded. Matching AV revision-7 BIT required; not included in this offline workspace.\r\n");
    rc=wait_reference_request();if(rc){rc=-110;goto cleanup;}
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
    if(m16_runtime_arm_pause_capture()){rc=-115;goto cleanup;}
#endif
    mic16_session_init(&session);XTime_GetTime(&begin);g_mic16_av2_result.stage=2;
    memcpy(raw_packets,_binary_saved_environment_reference_bin_start,512u*1024u);session.phase=M16_SESSION_WAIT_AUDIO;session.stored=512;
    while(session.phase!=M16_SESSION_DONE) {
        XTime_GetTime(&now);if(now-begin>300ULL*COUNTS_PER_SECOND){rc=-111;break;}
        packet_command=session.received>=4096u?'A':0; /* 丢弃启动瞬态，不保存新的背景参考。 */
        rc=mic16_receive_one(&rx_state,&ops,M16_RX_CAPACITY,M16_RX_EXPECTED,COUNTS_PER_SECOND/10u,&r);
        g_mic16_av2_result.last_packet=r;
        if(rc)break;
        if(session_rc<0){rc=-113;break;}
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
        if(session.stored==512 && session.phase==M16_SESSION_WAIT_AUDIO)break;
#endif
    }
cleanup:
    stop_source(0);
    if(!rx_state.quarantined) {
        g_mic16_av2_result.final_reset_ok=reset_checked(0);
        if(!g_mic16_av2_result.final_reset_ok){rx_state.quarantined=1;if(!rc)rc=-102;}
    }
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
    m16_runtime_arm_resume_capture();
#endif
    g_mic16_av2_result.reference_packets=session.stored<512?session.stored:512;
    g_mic16_av2_result.audio_packets=session.stored>512?session.stored-512:0;
    g_mic16_av2_result.discarded=session.discarded;g_mic16_av2_result.received=session.received;
    g_mic16_av2_result.warmup=session.warmup;
    g_mic16_av2_result.early_start=session.early_start;g_mic16_av2_result.audio_first_sequence=session.audio_first_sequence;
    printf("AV2_CAPTURE_STOP rc=%d stored=%u discarded=%u early_A=%u last_sr=0x%08lx actual=%u quarantine=%u\r\n",
        rc,session.stored,session.discarded,session.early_start,(unsigned long)g_mic16_av2_result.last_packet.last.sr,
        g_mic16_av2_result.last_packet.actual,rx_state.quarantined);
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    if(!rc && session.phase==M16_SESSION_WAIT_AUDIO && session.stored==512) {
        printf("IRQ_REFERENCE_READY; start sources then send A within300s; X cancels.\r\n");
        begin=ticks(0);
        do{M16_COOPERATE();packet_command=command_now();if(packet_command=='A'||packet_command=='a')break;if(packet_command=='X'){rc=-112;break;}}
        while(ticks(0)-begin<30ULL*COUNTS_PER_SECOND);
        if(!rc && packet_command!='A' && packet_command!='a')rc=-219;
        session.audio_first_sequence=512;
        if(!rc){g_mic16_av2_result.stage=3;rc=replay(&config);}
    }else if(!rc)rc=-114;
#else
    if(!rc && session.phase==M16_SESSION_DONE && session.stored==8192) {
        g_mic16_av2_result.stage=3;rc=replay(&config);
    } else if(!rc)rc=-114;
#endif
publish:
    g_mic16_av2_result.code=(u32)rc;g_mic16_av2_result.quarantined=rx_state.quarantined;
#if defined(MIC16_STREAM_IRQ) && MIC16_STREAM_IRQ
    g_mic16_av2_result.quarantined|=stream_irq_capture.ring.quarantined||stream_irq_capture.reset_failed;
#endif
    g_mic16_av2_result.stage=rc?6:5;
    printf("MIC16_AV2_%s computed=%lu rc=%d\r\n",rc?"STOP":"BOUNDED_TRIAL_DONE",(unsigned long)g_mic16_av2_result.computed,rc);
    printf("UDP_SUBMISSION_NOT_DELIVERY_REALTIME_OR_PHYSICAL_AV_ACCEPTANCE\r\n");
parked:
    /* DMA所有权被隔离后不刷新数据缓冲区；诊断结果仍由CPU拥有。 */
    Xil_DCacheFlushRange((INTPTR)&g_mic16_av2_result,sizeof(g_mic16_av2_result));
    {
        u64 previous=ticks(0);
        for(;;) {
            u64 current=ticks(0);
            if(current-previous>=2ULL*COUNTS_PER_SECOND) {
                previous=current;av_diagnostics();
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
                m16_runtime_arm_diagnostics();
#endif
                m16_network_poll();
                Xil_DCacheFlushRange((INTPTR)&g_mic16_av2_video,sizeof(g_mic16_av2_video));
            }
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
            M16_COOPERATE();
#else
            m16_network_poll();
#endif
            __asm__ volatile("nop");
        }
    }
}
