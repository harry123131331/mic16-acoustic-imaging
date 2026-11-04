#ifndef MIC16_PROJECTION_RUNTIME_H
#define MIC16_PROJECTION_RUNTIME_H
#include "mic16_projection_protocol.h"
enum {M16_PR_IDLE=0,M16_PR_PREPARE=1,M16_PR_DEVICE=2,M16_PR_COLLECT=3,M16_PR_COMPLETE=4,M16_PR_QUARANTINE=5};
enum {M16_PR_INVALID=-300,M16_PR_BUSY=-301,M16_PR_ABI=-302,M16_PR_GUARD=-303,
      M16_PR_CLOCK=-304,M16_PR_TIMEOUT=-305,M16_PR_RETURN=-306,M16_PR_RECEIPT=-307,M16_PR_CHANGED=-308,M16_PR_LEASE=-309,M16_PR_QUEUE=-310};
typedef struct {
    void *context;
    uint32_t (*read)(void *,unsigned);
    void (*write)(void *,unsigned,uint32_t);
    void (*flush)(void *,const void *,unsigned);
    void (*invalidate)(void *,const void *,unsigned);
    uint64_t (*ticks)(void *);
    void (*barrier)(void *);
    uint32_t arena_address;
    uint64_t timeout_ticks;
} m16_projection_io;
typedef struct {unsigned begin,words;} m16_projection_span;
typedef struct {
    uint64_t prepare_ticks,device_ticks,collect_ticks,total_ticks;
    unsigned flush_calls,invalidate_calls,last_control;
    int kernel_return,error;
} m16_projection_report;
/* 独占arena包含首尾各64字节保护区，整体64字节对齐。外部调度器在retire/cancel
 * 确认释放前，不得写输入/描述符，不得让DMA或另一任务复用任何缓存行。
 * generic IO的物理地址须由平台绑定证明对应同一arena；ABI常量不是芯片身份探测。 */
typedef struct {
    const void *client;
    unsigned priority;
    uint64_t order,granted_at;
} m16_projection_waiter;
typedef struct {
    uint32_t magic,phase,descriptors,jobs,span_count,span_index,span_cursor,done_seen;
    uint64_t *arena;
    unsigned words;
    m16_projection_io io;
    m16_projection_job plan[8];
    m16_projection_span spans[35];
    uint64_t snapshot[64],tag_high,started,submitted,collected,last_tick;
    m16_projection_report report;
    /* 仲裁从第一次写arena前开始，覆盖装包、设备运行和结果消费。
     * revision在不同生产者交接时递增，即使没有提交也使旧缓存失效。 */
    unsigned arbitrated;
    const void *producer,*previous_producer;
    uint64_t arena_revision,request_order,grants;
    m16_projection_waiter waiters[8];
} m16_projection_runtime;
#ifdef __cplusplus
extern "C" {
#endif
/* 首次零对象。仅绑定已核验新BIT/XSA的版本3，保护相机0x10000000以上保留区。
 * 不访问/复用旧统计核寄存器，不复位硬件来掩盖活动所有权。 */
int m16_projection_runtime_init(m16_projection_runtime *,const m16_projection_io *,uint64_t *,unsigned,unsigned verified_abi);
int m16_projection_runtime_available(const m16_projection_runtime *);
/* 仅在空闲时显式启用。8个有界等待者，优先级0～7，等待每8次授予提升一级。
 * 这限制授予次数中的饥饿，不代表尚未测量的墙钟截止期。 */
int m16_projection_runtime_enable_arbiter(m16_projection_runtime *);
int m16_projection_runtime_claim(m16_projection_runtime *,const void *client,unsigned priority);
int m16_projection_runtime_owns(const m16_projection_runtime *,const void *client);
/* 客户端既不拥有arena，也不在等待队列；其它生产者忙不妨碍本客户端安全释放。
 * 硬件隔离时保持不可释放；未启用仲裁时仍执行原全局idle约束。 */
int m16_projection_runtime_client_released(const m16_projection_runtime *,const void *client);
int m16_projection_runtime_schedulable(const m16_projection_runtime *);
int m16_projection_runtime_prepare_available(const m16_projection_runtime *,const void *client);
/* 仅归还尚未提交的CPU装包租约，不能据此归还运行中的设备。 */
int m16_projection_runtime_unclaim(m16_projection_runtime *,const void *client);
int m16_projection_runtime_submit_for(m16_projection_runtime *,const void *,unsigned,unsigned);
int m16_projection_runtime_poll_for(m16_projection_runtime *,const void *);
int m16_projection_runtime_retire_for(m16_projection_runtime *,const void *);
int m16_projection_runtime_cancel_for(m16_projection_runtime *,const void *);
/* submit只预检并登记，不清扫大块缓存、不启动忙等。 */
int m16_projection_runtime_submit(m16_projection_runtime *,unsigned descriptors,unsigned jobs);
/* 每次最多维护1KiB缓存，或检查一次设备完成；1进行中、0结果完整、负数故障隔离。
 * ap_done读清零会被记住，ap_idle且无start/auto_restart后才失效缓存、核对标签。
 * 不返回指向借用输入的伪完成，也不在poll内调用网络/旧整窗compute。 */
int m16_projection_runtime_poll(m16_projection_runtime *);
/* COMPLETE后显式归还整个arena。超时/时钟/内存异常隔离后禁止归还或重新init，
 * 必须由未来平台复位流程证明硬件停止；当前模块不提供绕过隔离的重置接口。 */
int m16_projection_runtime_retire(m16_projection_runtime *);
/* PREPARE且硬件仍idle时可取消并归还，返回1；运行/收取中返回0，须继续poll排空。 */
int m16_projection_runtime_cancel(m16_projection_runtime *);
#ifdef __cplusplus
}
#endif
#endif
