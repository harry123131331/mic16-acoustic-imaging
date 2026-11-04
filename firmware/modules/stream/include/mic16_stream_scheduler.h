#ifndef MIC16_STREAM_SCHEDULER_H
#define MIC16_STREAM_SCHEDULER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define M16_STREAM_MAX_SLOTS 3u
#define M16_STREAM_WINDOW 40960u
enum {M16_STREAM_OK=0,M16_STREAM_EMPTY=1,M16_STREAM_BUSY=2,
    M16_STREAM_INVALID=-60,M16_STREAM_LEASE=-61,M16_STREAM_STOPPED=-62,
    M16_STREAM_OVERRUN=-63,M16_STREAM_GAP=-64,M16_STREAM_EXPIRED=-65,
    M16_STREAM_CLOCK=-66,M16_STREAM_SEND_FAILED=-67,M16_STREAM_CANCELLED=-68,
    M16_STREAM_NO_RESULT=-69,M16_STREAM_COUNTER=-70,M16_STREAM_INPUT_INVALID=-71};
enum {M16_STREAM_INPUT=1,M16_STREAM_OUTPUT=2};
enum {M16_STREAM_FREE=0,M16_STREAM_CAPTURE=1,M16_STREAM_READY=2,
    M16_STREAM_COMPUTE=3,M16_STREAM_BUILD=4,M16_STREAM_SEND=5};
typedef struct {
    unsigned input_slots,output_slots;
    uint64_t tick_hz,max_input_wait_ticks,max_result_age_ticks,context_id,reference_id;
} mic16_stream_config;
typedef struct {
    unsigned pool,slot,owner,epoch;
    uint64_t generation;
} mic16_stream_ticket;
typedef struct {
    uint64_t first_sample,end_sample,received_tick,context_id,reference_id;
} mic16_stream_window;
typedef struct {
    unsigned state,result_reserved;
    uint64_t generation;
    mic16_stream_window window;
} mic16_stream_slot;
typedef struct {
    uint64_t inputs_committed,inputs_acquired,outputs_reserved,outputs_committed,outputs_sent;
    uint64_t input_overruns,output_backpressure,epochs_failed,queued_discarded,lease_rejections;
    unsigned peak_input_slots,peak_output_slots;
} mic16_stream_stats;
/* 只持有有界元数据，不分配/复制PCM和音频。真实缓冲由调用方提供且须互不重叠。
 * 必须零初始化后init；所有方法由单一调度所有者串行调用，或由外部锁完整保护。
 * 不是ISR/多核安全队列；不能靠abort或归还接口让仍在访问内存的DMA/HLS停止。 */
typedef struct {
    unsigned magic,epoch,reference_invalid;
    int failure;
    mic16_stream_config config;
    uint64_t last_tick,generation,next_input_first,next_compute_first,next_output_window;
    mic16_stream_slot input[3],output[3];
    mic16_stream_stats stats;
} mic16_stream_scheduler;
int mic16_stream_init(mic16_stream_scheduler *,const mic16_stream_config *,unsigned epoch,uint64_t now);
/* first从新epoch的0开始；received_tick是CPU确认收到完整窗的时刻，不是已验证物理采样时间。
 * 输入无空闲槽意味着当前窗无法接收，锁存OVERRUN；已有未提交采集租约则返回BUSY。 */
int mic16_stream_input_reserve(mic16_stream_scheduler *,uint64_t first,uint64_t now,mic16_stream_ticket *);
int mic16_stream_input_commit(mic16_stream_scheduler *,const mic16_stream_ticket *,uint64_t now);
/* 整窗DMA在IOC之后还有有界缓存/归一化。received_tick保留IOC确认时刻，
 * 不允许未来时刻，也不能用提交时刻抹掉排队/转换延迟；仍不是物理采样时间。 */
int mic16_stream_input_commit_received(mic16_stream_scheduler *,const mic16_stream_ticket *,
    uint64_t received_tick,uint64_t now);
int mic16_stream_input_acquire(mic16_stream_scheduler *,uint64_t now,mic16_stream_ticket *,mic16_stream_window *);
/* 计算开始前先为该窗预留独立结果槽；无空闲结果槽返回BUSY，形成反压而不覆盖旧输出。
 * 结果可保持BUILD以等待后一窗重建；不能在input_release之后才拿旧输入租约预留结果。 */
int mic16_stream_output_reserve(mic16_stream_scheduler *,const mic16_stream_ticket *input,
    uint64_t now,mic16_stream_ticket *output);
/* 显式归还表示该所有者已经停止访问缓冲，无需时钟也能在故障后安全收回租约。
 * 取消正常采集、或计算未预留任何结果就归还，均中止当前epoch，不能悄悄跳窗。 */
int mic16_stream_input_release(mic16_stream_scheduler *,const mic16_stream_ticket *);
int mic16_stream_output_commit(mic16_stream_scheduler *,const mic16_stream_ticket *,uint64_t now);
int mic16_stream_output_acquire(mic16_stream_scheduler *,uint64_t now,mic16_stream_ticket *,mic16_stream_window *);
/* 发送前及分包期间均可检查；不能撤回此前已经交给网络的数据。 */
int mic16_stream_output_current(mic16_stream_scheduler *,const mic16_stream_ticket *,uint64_t now);
int mic16_stream_output_finish(mic16_stream_scheduler *,const mic16_stream_ticket *,uint64_t now,int sent_ok);
int mic16_stream_output_cancel(mic16_stream_scheduler *,const mic16_stream_ticket *);
int mic16_stream_poll(mic16_stream_scheduler *,uint64_t now);
/* 首个原因锁存；仅撤销调度器自有READY队列，不覆盖CAPTURE/COMPUTE/BUILD/SEND租约。
 * 上层须停止采集、清跟踪器和重建时段，等待所有活动所有者停止并明确归还。
 * 重启须更大epoch；输入无效或姿态改变还须新的参考ID。ID不证明参考已实际采好。 */
int mic16_stream_abort(mic16_stream_scheduler *,int reason);
int mic16_stream_restart(mic16_stream_scheduler *,unsigned epoch,uint64_t context_id,uint64_t reference_id,uint64_t now);
#ifdef __cplusplus
}
#endif
#endif
