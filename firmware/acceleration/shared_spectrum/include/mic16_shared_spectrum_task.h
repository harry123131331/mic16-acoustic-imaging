#ifndef MIC16_SHARED_SPECTRUM_TASK_H
#define MIC16_SHARED_SPECTRUM_TASK_H
#include "mic16_shared_spectrum_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {M16_SS_IDLE=0,M16_SS_CPU=1,M16_SS_VALIDATE_INPUT=2,M16_SS_GUARDS=3,
 M16_SS_FLUSH=4,M16_SS_START=5,M16_SS_DEVICE=6,M16_SS_INVALIDATE=7,
 M16_SS_CHECK=8,M16_SS_COMPLETE=9,M16_SS_STOPPED=10,M16_SS_SAFE_ERROR=11,M16_SS_QUARANTINE=12};
enum {M16_SS_INVALID=-540,M16_SS_BUSY=-541,M16_SS_SEQUENCE=-542,M16_SS_INPUT=-543,
 M16_SS_OUTPUT=-544,M16_SS_CLOCK=-545,M16_SS_TIMEOUT=-546,M16_SS_GUARD=-547,
 M16_SS_RETURN=-548,M16_SS_DEVICE_STATE=-549};
typedef struct {
 unsigned magic,phase,active,output_bytes,tail_word,cursor,region,stop_requested,seen_done,started;
 unsigned last_control,max_cache_bytes;
 int error,kernel_return;
 uint64_t epoch,job,last_epoch,last_job,last_tick,device_tick,steps;
 m16_shared_runtime *owner;
} m16_shared_spectrum_task;
/* 复用已初始化的模式4引擎、原arena与已核验寄存器布局，不新增硬件。
 * 同一事件循环独占此节点；owner.busy同时阻止旧同步入口。不能绕过接口直接清busy。
 * 这里只负责已打包的FFT事务；原FP64均值/能量/Hann及弱值回退由生产节点保留。 */
int m16_shared_spectrum_task_init(m16_shared_spectrum_task *,m16_shared_runtime *);
int m16_shared_spectrum_task_available(const m16_shared_spectrum_task *);
/* 成功后CPU取得arena租约，可调用原pack_contiguous；仅CPU阶段允许写输入。
 * epoch/job是本地事务元数据，该旧FFT硬件没有tag回执，不据此声称硬件身份校验。 */
int m16_shared_spectrum_task_acquire(m16_shared_spectrum_task *,uint64_t epoch,uint64_t job,unsigned active);
uint64_t *m16_shared_spectrum_task_arena(m16_shared_spectrum_task *);
/* 完成且仍持有当前事务时只读借出807×active复数；retire后失效。 */
const uint64_t *m16_shared_spectrum_task_output(const m16_shared_spectrum_task *,uint64_t epoch,uint64_t job);
int m16_shared_spectrum_task_submit(m16_shared_spectrum_task *);
/* 每次最多验证64个输入/输出值、维护1KiB缓存或做一次设备检查，无整窗等待循环。
 * 1继续、0完成、2停止、负数故障。COMPLETE/STOPPED/SAFE_ERROR仍须retire归还arena。 */
int m16_shared_spectrum_task_poll(m16_shared_spectrum_task *);
int m16_shared_spectrum_task_stop(m16_shared_spectrum_task *);
int m16_shared_spectrum_task_retire(m16_shared_spectrum_task *,uint64_t epoch,uint64_t job);
int m16_shared_spectrum_task_releasable(const m16_shared_spectrum_task *);
#ifdef __cplusplus
}
#endif
#endif
