#ifndef MIC16_FFT64_TASK_H
#define MIC16_FFT64_TASK_H
#include "mic16_projection_runtime.h"
#include "mic16_fft64_range_guard.h"
enum {M16_F64_IDLE=0,M16_F64_INPUT=1,M16_F64_TWIDDLE=2,M16_F64_SUBMIT=3,
 M16_F64_DEVICE=4,M16_F64_CHECK=5,M16_F64_COPY=6,M16_F64_COMPLETE=7,M16_F64_STOPPED=8,M16_F64_FAILED=9,M16_F64_CPU_FALLBACK=10};
enum {M16_F64_MAX_GUARDS=16};
enum {M16_F64_INVALID=-1400,M16_F64_ABI=-1401,M16_F64_INPUT_ERROR=-1402,
 M16_F64_OUTPUT_ERROR=-1403,M16_F64_OWNERSHIP=-1404};
/* 同一运行器仅由外层事件循环串行选择一个生产者；request取得客户端所有权后，
 * 其它投影/统计生产者不得在其归还前装包。输入及旋转表借用至完成/安全取消。
 * service须在常驻主循环中持续调用，STOP后也必须排空设备，不能靠清对象解锁。 */
typedef struct {
 unsigned magic,phase,n,table,inverse,cursor,input_at,output_at,twiddle_at,cancel_requested,last_items;
 int error;
 uint64_t tag,request_tag,steps,transactions;
 const void *client,*pending_client;
 double *real,*imaginary;
 const double *twiddle_real,*twiddle_imaginary;
 m16_projection_runtime *runtime;
 unsigned guard_count,guard_index,fallback_reason;
 uint64_t range_fallbacks,coefficient_fallbacks,missing_table_fallbacks;
 m16_fft64_input_range input_range;
 m16_fft64_range_guard guards[M16_F64_MAX_GUARDS];
 m16_fft64_range_guard preparing_guard;
 int16_t guard_scratch[4096];
} m16_fft64_task;
#ifdef __cplusplus
extern "C" {
#endif
int m16_fft64_init(m16_fft64_task *,m16_projection_runtime *,unsigned verified_abi);
/* 在初始化/无客户端借用时为表注册保守格点界；同一键重注册会重算。
 * 0已登记，2该表不适合PL，负数参数无效；每次装载仍逐系数检查，不靠地址猜内容。
 * 表可在请求之间重建；请求内的数组/表继续遵循原借用约束。 */
int m16_fft64_register_twiddles(m16_fft64_task *,const double *,const double *,unsigned n,unsigned table);
/* 0登记成功，1设备忙需重试，2不持有资源且输入未改，走原CPU；负数拒绝。 */
int m16_fft64_request(m16_fft64_task *,const void *client,double *,double *,unsigned n,
                     const double *,const double *,unsigned table_length,unsigned inverse);
/* 每步32个复数拷贝/检查，或运行器最多1KiB维护；无需阻塞等待。 */
int m16_fft64_service(m16_fft64_task *);
/* poll返回2表示安全归还且原输入未改，客户端必须恢复原CPU蝶形。 */
int m16_fft64_poll(m16_fft64_task *,const void *client);
/* 1已释放，0等待排空，负数硬件仍隔离。取消中的输出不得发布。 */
int m16_fft64_cancel(m16_fft64_task *,const void *client);
int m16_fft64_owns(const m16_fft64_task *,const void *client);
#ifdef __cplusplus
}
#endif
#endif
