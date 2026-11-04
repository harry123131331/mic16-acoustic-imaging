#ifndef MIC16_AUDIO_RESIDENT_TASK_H
#define MIC16_AUDIO_RESIDENT_TASK_H
#include "mic16_audio_pipeline_task.h"
#include "mic16_capture_stream.h"
#include "mic16_output_pool.h"
#include "mic16_resident_control.h"
#include "mic16_pcm_wire.h"

enum {M16_AR_IDLE=0,M16_AR_STARTING=1,M16_AR_RUNNING=2,M16_AR_FINISH_INPUT=3,
 M16_AR_FINISH_AUDIO=4,M16_AR_FINISH_OUTPUT=5,M16_AR_RESETTING=6,M16_AR_STOPPED=7,
 M16_AR_DRAIN_ERROR=8,M16_AR_FAILED=9};
enum {M16_AR_INVALID=-1040,M16_AR_BUSY=-1041,M16_AR_SEQUENCE=-1042,M16_AR_OUTPUT=-1043};
typedef struct {
 void *context;
 /* 同步深复制/消费本包，0=已交给网络，1=反压，负数=故障。
  * 不得保留指针后返回0，不得递归进入本任务，不得在回调中运行算法。 */
 int (*try_send)(void *,unsigned port,const unsigned char *,unsigned bytes);
} m16_audio_resident_io;
/* 常驻任务只持有控制数据。完整工作区、3个原始输入槽、3个独立输出槽均外置DDR。
 * 每次step：一个采集片段、一个输出片段、最多一包发送、一个计算图片段。
 * 真实设备并行运行；本层不包装旧整窗compute，也没有有限窗数自动退出。 */
typedef struct {
 unsigned magic,phase,epoch,used,in_step;
 unsigned input_held,output_reserved,output_bound,computing,finishing;
 unsigned location_started,location_held,audio_copying,audio_pending,stop_sent;
 int error;
 uint32_t pending_audio_window;
 uint64_t computed_windows,locations_handed,packets_sent,backpressure,steps;
 uint64_t begin_tick,detection_end_tick,last_window,last_detection_us,last_compute_us;
 const float *reference;
 char pose_reference_key[512];
 /* 可选后端是外置常驻对象；停止/故障期间仍服务，直至所有借用归还。 */
 void *backend_context;
 uintptr_t backend_bytes;
 int (*service_backend)(void *);
 m16_audio_resident_io io;
 m16_capture_stream *capture;
 m16_audio_pipeline_task *pipeline;
 m16_output_pool *output;
 m16_resident_control *control;
 m16_capture_stream_view input;
 mic16_stream_ticket build;
 m16_pcm_wire raw_pcm;
 unsigned raw_active,raw_frame,location_committed;
 uint64_t raw_frames_sent,raw_packets_sent;
} m16_audio_resident_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 所有子节点已初始化且空闲，scheduler/pipeline/output准备epoch=1，control在epoch=0。
 * 参考8192×16来自明确提供的缓存，不自动采背景；key与源ID节点初始key一致。
 * 虚拟区域及采集/设备物理arena核验互不覆盖，不能替代最终ELF/平台地址绑定审计。 */
int m16_audio_resident_init(m16_audio_resident_task *,m16_capture_stream *,m16_audio_pipeline_task *,
 m16_output_pool *,m16_resident_control *,const float *reference,const char *key,const m16_audio_resident_io *);
/* 外部主循环先有限服务网络/control UDP，再step；IRQ仅进入底层capture_irq。
 * 0=当前待机/已正常停止，1=推进/运行，负数=已锁存故障。
 * 故障后仍须重复step服务排空；releasable前不得退出、重载或清对象。 */
/* 仅首次待机时绑定一次；后端区域须与全部输入/工作区/设备arena分离。 */
int m16_audio_resident_bind_backend(m16_audio_resident_task *,void *,uintptr_t,int (*)(void *));
int m16_audio_resident_step(m16_audio_resident_task *);
int m16_audio_resident_releasable(const m16_audio_resident_task *);
/* 计算时间是完整图墙钟间隔（含增强/交接等待），不等于CPU占用，也不等于旧检测基线。
 * last_detection_us另存检测墙钟。物理采样至电脑显示的延迟仍未核验。 */
int m16_audio_resident_progress(const m16_audio_resident_task *,m16_control_progress *);
#ifdef __cplusplus
}
#endif
#endif
