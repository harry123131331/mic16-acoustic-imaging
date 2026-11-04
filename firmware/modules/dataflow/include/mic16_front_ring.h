#ifndef MIC16_FRONT_RING_H
#define MIC16_FRONT_RING_H
#include "mic16_public_spectrum_task.h"
#include "mic16_spatial_evidence_cache.h"

#define M16_FR_FRAMES 20u
#define M16_FR_PCM_VALUES (2048u*16u)
enum {M16_FR_IDLE=0,M16_FR_COPY=1,M16_FR_FRONT=2,M16_FR_STOPPING=3,M16_FR_STOPPED=4,M16_FR_FAILED=5,M16_FR_SPATIAL=6};
enum {M16_FR_INVALID=-1500,M16_FR_BUSY=-1501,M16_FR_SEQUENCE=-1502,M16_FR_INPUT=-1503,M16_FR_LEASE=-1504,M16_FR_EMPTY=-1505,M16_FR_CLOCK=-1506};

typedef struct {
 uint64_t epoch,frame_seq,first_sample,received_tick,front_done_tick;
 unsigned ready;
} m16_front_frame_metadata;

/* 存储由平台一次分配；保留原数据格式，不改变精度，不在IRQ复制。
 * PCM用于组回同一20帧上下文和全部源增强；频谱/PHAT只对新帧计算。 */
typedef struct {
 float pcm[M16_FR_FRAMES][M16_FR_PCM_VALUES];
 mic16_spectral_frame spectrum[M16_FR_FRAMES];
 m16_spatial_evidence_storage evidence;
} m16_front_ring_storage;

typedef struct {
 const void *owner;
 uint64_t epoch,generation,first_frame;
 unsigned head;
} m16_front_context_lease;

typedef struct {
 unsigned magic,phase,head,valid_frames,cursor,reader;
 int error;
 uint64_t epoch,next_frame,generation,last_tick,last_received_tick,frames_in,frames_out,contexts;
 const float *input;
 m16_front_frame_metadata metadata[M16_FR_FRAMES];
 m16_front_ring_storage *storage;
 mic16_spectral_workspace *work;
 m16_public_spectrum_task front;
 unsigned evidence_enabled;
 m16_spatial_evidence_cache evidence;
} m16_front_ring;

#ifdef __cplusplus
extern "C" {
#endif
/* 只在初始停止态绑定一次；从此粗扫由本前端独占，后端必须使用准备好的粗图。 */
int m16_front_ring_bind_evidence(m16_front_ring *,m16_coarse_scan_runtime *);
int m16_front_ring_init(m16_front_ring *,m16_front_ring_storage *,mic16_spectral_workspace *);
/* START只清元数据，Hann/旋转表仍常驻。epoch必须严格递增。 */
int m16_front_ring_start(m16_front_ring *,uint64_t epoch,uint64_t tick);
int m16_front_ring_begin(m16_front_ring *,const float *,unsigned values,uint64_t epoch,
 uint64_t frame_seq,uint64_t first_sample,uint64_t received_tick);
/* 单次诊断已将本帧PCM保存于storage->pcm[head]，在同一存储上执行原FFT/PHAT。
 * 仅初始20帧可用；调用者必须先完成全部采集并验证样本轴，不能用于伪造持续输入。 */
int m16_front_ring_begin_buffered(m16_front_ring *,uint64_t epoch,uint64_t frame_seq,uint64_t received_tick);
/* 先用每步64项复制释放DMA输入，再逐步执行原FFT/PHAT。 */
int m16_front_ring_step(m16_front_ring *,uint64_t tick);
int m16_front_ring_input_releasable(const m16_front_ring *);
/* 同一20帧的只读快照；持有时拒绝覆盖。调用者应有界搬到既有后端缓冲后立即归还，
 * 不能拿着此租约等待整窗后端计算。该限制显式暴露背压，不能假装无丢帧实时。 */
int m16_front_ring_acquire(m16_front_ring *,m16_front_context_lease *);
int m16_front_ring_frame(const m16_front_ring *,const m16_front_context_lease *,unsigned index,
 const m16_front_frame_metadata **,const float **,const mic16_spectral_frame **);
int m16_front_ring_release(m16_front_ring *,const m16_front_context_lease *);
int m16_front_ring_stop(m16_front_ring *);
int m16_front_ring_releasable(const m16_front_ring *);
#ifdef __cplusplus
}
#endif
#endif
