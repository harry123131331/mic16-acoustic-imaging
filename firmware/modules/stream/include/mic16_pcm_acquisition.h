#ifndef MIC16_PCM_ACQUISITION_H
#define MIC16_PCM_ACQUISITION_H
#include "mic16_window_capture.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define M16_PA_FRAMES 20u
#define M16_PA_VALUES (2048u*16u)
enum {M16_PA_INVALID=-2300,M16_PA_SEQUENCE=-2301,M16_PA_EXPIRED=-2302};
typedef struct {
 uint64_t epoch,frame,first_sample,received_tick;
 unsigned ready;
} m16_pcm_frame_metadata;
typedef struct {
 unsigned magic,active,held,copy_cursor,finish_requested,closing;
 int error;
 uint64_t epoch,next_frame,frames_taken,leases_released,copy_steps,aborted_partial_frames;
 uint64_t capture_completed_begin,warmup_completed_begin;
 m16_window_capture *capture;
 float (*pcm)[M16_PA_VALUES];
 m16_window_capture_lease lease;
 m16_pcm_frame_metadata metadata[M16_PA_FRAMES];
} m16_pcm_acquisition;

/* 仅绑定原20帧PCM存储；不持有FFT/SRP任务，不分配第二套音频环。 */
int m16_pcm_acquisition_init(m16_pcm_acquisition *,m16_window_capture *,float (*)[M16_PA_VALUES]);
int m16_pcm_acquisition_start(m16_pcm_acquisition *,uint64_t epoch);
/* 每步原驱动一个片段及最多1KiB CPU复制；不等待定位、网络或整窗结果。 */
int m16_pcm_acquisition_step(m16_pcm_acquisition *);
/* 返回指针仅在下一次生产者step前有效；消费者须分块复制并每步复核帧号。 */
int m16_pcm_acquisition_frame(const m16_pcm_acquisition *,uint64_t epoch,uint64_t frame,
 const m16_pcm_frame_metadata **,const float **);
/* 正常停止：在完整DMA包边界结束，保留所有完成帧供归一化、复制及RAW排空。 */
int m16_pcm_acquisition_request_finish(m16_pcm_acquisition *);
int m16_pcm_acquisition_input_complete(const m16_pcm_acquisition *);
/* 外层确认RAW及设备排空后，才最终复位DMA并报告可重启。 */
int m16_pcm_acquisition_finish_stop(m16_pcm_acquisition *);
int m16_pcm_acquisition_releasable(const m16_pcm_acquisition *);
int m16_pcm_acquisition_restart(m16_pcm_acquisition *);
/* 故障取消不伪装无丢样；未完成复制单独计数。 */
int m16_pcm_acquisition_abort(m16_pcm_acquisition *,int reason);
#ifdef __cplusplus
}
#endif
#endif
