#ifndef MIC16_AV_RUNTIME_H
#define MIC16_AV_RUNTIME_H
#include "mic16_video_export.h"
/* 同步回调不保留缓冲区、不调用音频算法，不从中断调用本模块。 */
typedef struct {
 void *context;
 uint64_t (*now_us)(void *);
 void (*network_poll)(void *);
 int (*video_send)(void *,const unsigned char *,unsigned);
} m16_av_runtime_ops;
typedef struct {
 uint64_t checkpoints,steps,max_checkpoint_gap_us,max_service_us;
 uint64_t video_bytes,audio_bytes,budget_denied,reentries;
 uint32_t audio_packets,capture_pauses,cancelled_frames;
} m16_av_runtime_stats;
typedef struct {
 m16_av_runtime_ops ops;
 m16_video_export *video;
 m16_av_runtime_stats stats;
 uint64_t last_now,last_checkpoint,last_step,credit_time,credits,audio_debt;
 unsigned rate,burst,interval_us,started,active,paused,audio_depth,fault;
} m16_av_runtime;
/* 仅初始化全新或已停止的对象。burst是字节，不是帧；预算不构成网口实时保证。 */
int m16_av_runtime_init(m16_av_runtime *,m16_video_export *,const m16_av_runtime_ops *,unsigned,unsigned,unsigned);
void m16_av_runtime_step(void *);
int m16_av_runtime_video_send(void *,const unsigned char *,unsigned);
/* 暂停会丢弃未完成快照并同步归还相机槽，返回成功后才能开始轮询式音频DMA。 */
int m16_av_runtime_pause(m16_av_runtime *);
int m16_av_runtime_resume(m16_av_runtime *);
int m16_av_runtime_audio_begin(m16_av_runtime *);
int m16_av_runtime_audio_end(m16_av_runtime *,unsigned,int);
#endif
