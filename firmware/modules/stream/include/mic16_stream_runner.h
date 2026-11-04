#ifndef MIC16_STREAM_RUNNER_H
#define MIC16_STREAM_RUNNER_H
#include "mic16_stream_scheduler.h"
/* 主循环运行器：采集可以异步进行；计算回调通过checkpoint服务采集和分包发送。
 * 所有方法仍由同一个CPU上下文串行调用，禁止从DMA中断直接调用。
 * 设备回调只操作自己的载荷/完成邮箱，不得递归进入运行器。
 * 这里只验证运行合同，不保证硬件DMA中断时限或实时吞吐。 */
enum {M16_RUNNER_MORE=0,M16_RUNNER_DONE=1,M16_RUNNER_QUIESCING=3};
typedef struct mic16_stream_runner mic16_stream_runner;
typedef struct {
    void *context;
    uint64_t (*ticks)(void *);
    /* 返回0表示已启动；任何非零均是故障。 */
    int (*capture_start)(void *,unsigned slot,const mic16_stream_window *);
    /* 返回0=仍在采集、1=整窗完整且设备不再写该槽、负数=故障。 */
    int (*capture_poll)(void *);
    /* 返回1才证明设备已停止访问；0/负数均保留CAPTURE租约，禁止复用。 */
    int (*capture_stop)(void *);
    /* 计算成功后返回累计已完整重建的窗数，不能在回调内部提前提交结果。
     * 一个输出可等待后一输入窗；compute/finish必须复制完所需输入才返回。
     * 计算期间应定期checkpoint；不支持抢占CPU计算或强制结束仍运行的HLS。 */
    int (*compute)(void *,mic16_stream_runner *,unsigned input_slot,
        const mic16_stream_window *,unsigned output_slot,uint64_t *emitted);
    int (*finish)(void *,mic16_stream_runner *,uint64_t *emitted);
    /* 每次最多发送一包：0=尚有剩余，1=整窗发送完成，负数=发送失败。
     * 返回前必须停止访问该次临时指针；完成不等于对端已收到。 */
    int (*send_step)(void *,unsigned output_slot,const mic16_stream_window *);
} mic16_stream_runner_ops;
typedef struct {
    mic16_stream_config queue;
    uint64_t capture_timeout_ticks;
    /* 0表示不设窗数上限；有限值用于离线回放/有界验收，不是实时性证明。 */
    uint64_t input_limit;
} mic16_stream_runner_config;
struct mic16_stream_runner {
    unsigned magic,in_step,in_checkpoint,in_callback,finished,finish_called,compute_held,capture_held,send_held;
    int backend_error;
    uint64_t capture_started,input_limit,capture_timeout_ticks,computed,emitted,checkpoint_calls;
    mic16_stream_scheduler queue;
    mic16_stream_runner_ops ops;
    mic16_stream_ticket capture,compute,send,build[3];
    mic16_stream_window compute_window,send_window;
};
/* 零初始化；配置、回调表和运行器内存必须分别存放，不得互相别名。 */
int mic16_stream_runner_init(mic16_stream_runner *,const mic16_stream_runner_config *,
    const mic16_stream_runner_ops *,unsigned epoch);
int mic16_stream_runner_step(mic16_stream_runner *);
int mic16_stream_runner_checkpoint(mic16_stream_runner *);
/* 仅返回仍由BUILD拥有的结果槽；供编码回调将报文暂存到独立载荷，绝不立即发布。 */
int mic16_stream_runner_build_slot(mic16_stream_runner *,uint64_t window,unsigned *slot);
/* 立即锁存原因并停止采集；如果DMA尚未停下，返回QUIESCING并保留租约。
 * 计算回调尚未返回时，不会抢先归还它正在读写的输入/结果。 */
int mic16_stream_runner_stop(mic16_stream_runner *,int reason);
#endif
