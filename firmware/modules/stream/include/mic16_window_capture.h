#ifndef MIC16_WINDOW_CAPTURE_H
#define MIC16_WINDOW_CAPTURE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define M16_WC_SAMPLES 40960u
#define M16_WC_FRAME_SAMPLES 2048u
#define M16_WC_FRAME_STORAGE (M16_WC_FRAME_SAMPLES*64u+128u)
#define M16_WC_VALUES (M16_WC_SAMPLES*16u)
#define M16_WC_BYTES (M16_WC_VALUES*4u)
#define M16_WC_STORAGE (M16_WC_BYTES+128u)
enum {M16_WC_PREPARING=1,M16_WC_READY=2,M16_WC_RUNNING=3,
 M16_WC_DRAINING=4,M16_WC_STOPPED=5,M16_WC_QUARANTINE=6,M16_WC_FINISHING=7};
enum {M16_WC_FREE=0,M16_WC_CLEAN=1,M16_WC_ARMED=2,M16_WC_DMA=3,
 M16_WC_DONE=4,M16_WC_INVALIDATE=5,M16_WC_NORMALIZE=6,M16_WC_PCM=7,M16_WC_READING=8};
enum {M16_WC_INVALID=-500,M16_WC_BUSY=-501,M16_WC_EMPTY=-502,M16_WC_LEASE=-503,
 M16_WC_DMA_ERROR=-504,M16_WC_LENGTH=-505,M16_WC_OVERFLOW=-506,M16_WC_CLOCK=-507,
 M16_WC_TIMEOUT=-508,M16_WC_GUARD=-509,M16_WC_RANGE=-510,M16_WC_COUNTER=-511,
 M16_WC_FENV=-512,M16_WC_STOPPING=-513};
typedef struct {
 void *context;
 uint32_t (*read)(void *,unsigned offset);
 void (*write)(void *,unsigned offset,uint32_t);
 uint32_t (*frontend_status)(void *); /* bit0=粘滞溢出，bit1=PDM时钟运行。 */
 void (*source_enable)(void *,unsigned); /* 0同时清音频FIFO，不复位相机或计算核。 */
 uint64_t (*ticks)(void *);
 void (*clean)(void *,void *,unsigned);
 void (*invalidate)(void *,void *,unsigned);
 uintptr_t (*lock)(void *); /* 同一CPU屏蔽本中断并返回旧状态；不得重置GIC。 */
 void (*unlock)(void *,uintptr_t);
 void (*barrier)(void *);
} m16_window_capture_io;
typedef struct {
 void *storage[3]; /* 各自64字节对齐，前后64字节保护区；payload可原地转float。 */
 uint32_t physical[3]; /* 指向保护区开头，物理区间也必须互不相交。 */
 unsigned scale_exponent,warmup_windows;
 unsigned char logical_to_wire[16];
 uint64_t transfer_timeout_ticks,reset_timeout_ticks;
 /* 合同必须与匹配BD/XSA核对；这些字段本身不是硬件身份或验收证据。 */
 /* packet_samples只接受40960或2048，必须与配套BIT的TLAST一致。
  * 2048模式每槽至少M16_WC_FRAME_STORAGE字节；warmup_windows按小帧包数计。 */
 unsigned packet_samples,dma_length_bits,fifo_streaming,local_fifo_reset;
} m16_window_capture_config;
typedef struct {
 unsigned state,cursor,discard;
 uint64_t generation,first_sample,received_tick;
} m16_window_capture_slot;
typedef struct {
 unsigned slot,epoch;
 uint64_t generation,first_sample,received_tick;
 const float *pcm;
} m16_window_capture_lease;
typedef struct {
 unsigned magic,phase,epoch,active,next_slot,warmup_left,reset_started,finish_requested;
 int failure;
 uint64_t generation,next_sample,next_take_sample,last_tick,transfer_tick,reset_tick;
 uint64_t completed,warmup_completed,delivered,irqs,spurious,finish_sample;
 uint32_t fault_sr,fault_cr,fault_length,fault_frontend;
 m16_window_capture_io io;
 m16_window_capture_config config;
 m16_window_capture_slot slots[3];
} m16_window_capture;
/* 不分配内存。所有前台接口由同一事件循环串行调用；中断只调用irq。
 * 每次step最多1KiB缓存维护或64个PCM值；IRQ不清缓存、不复制整窗、不跑浮点。
 * 必须独占此DMA、三个缓冲及音频enable。初始化要求DMA已halt，拒绝接管活动DMA。 */
int m16_window_capture_init(m16_window_capture *,const m16_window_capture_io *,const m16_window_capture_config *);
int m16_window_capture_step(m16_window_capture *); /* 1推进/等待，0准备完，2已安全停止，负数故障。 */
int m16_window_capture_start(m16_window_capture *,unsigned epoch);
void m16_window_capture_irq(m16_window_capture *);
int m16_window_capture_take(m16_window_capture *,m16_window_capture_lease *);
int m16_window_capture_release(m16_window_capture *,const m16_window_capture_lease *);
/* 正常结束：当前完整窗IOC后不再续接DMA，保留该窗及既有数据供step/take。
 * 与stop不同，不复位或丢弃未消费的完整窗；输入取尽后仍须stop确认DMA复位/halt。 */
int m16_window_capture_request_finish(m16_window_capture *);
/* 在指定完整样本边界停止；IRQ在续接前核对，防止前台观察错过边界。 */
int m16_window_capture_request_finish_at(m16_window_capture *,uint64_t sample_end);
/* 在IRQ锁内按当前在途包选择下一个完整上下文边界，避免前台与IOC竞态。 */
int m16_window_capture_request_finish_aligned(m16_window_capture *,uint64_t context_samples,uint64_t *sample_end);
/* 只表示不会再产生输入且已全部取出；计算/输出完成及设备可重启须另行核验。 */
int m16_window_capture_input_complete(const m16_window_capture *);
int m16_window_capture_stop(m16_window_capture *);
int m16_window_capture_releasable(const m16_window_capture *);
/* 只允许更大的epoch；DMA仍活动/复位未确认/计算持有租约时拒绝，不靠memset解除隔离。 */
int m16_window_capture_restart(m16_window_capture *);
#ifdef __cplusplus
}
#endif
#endif
