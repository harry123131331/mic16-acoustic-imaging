#ifndef MIC16_FIR_TASK_H
#define MIC16_FIR_TASK_H
#include "mic16_fir_provider.h"
#include "mic16_projection_runtime.h"
enum {M16_FIR_IDLE=0,M16_FIR_VALIDATE=1,M16_FIR_WAIT_SLOT=2,M16_FIR_PACK=3,
 M16_FIR_SUBMIT=4,M16_FIR_DEVICE=5,M16_FIR_CHECK=6,M16_FIR_COPY=7,
 M16_FIR_COMPLETE=8,M16_FIR_FALLBACK=9,M16_FIR_STOPPED=10,M16_FIR_FAILED=11};
enum {M16_FIR_INVALID=-1200,M16_FIR_ABI=-1201,M16_FIR_INPUT=-1202,
 M16_FIR_OWNERSHIP=-1203,M16_FIR_OUTPUT=-1204,M16_FIR_FLOAT_ENV=-1205};
typedef struct {
 unsigned magic,phase,priority,cursor,length,first_output,tile_outputs;
 unsigned first_input,tile_inputs,cancel_requested,unsafe,last_items,max_items;
 int error;
 const void *client;
 m16_fir_request request;
 m16_projection_runtime *runtime;
 uint64_t requests,transactions,steps,input_words,output_words;
 uint64_t expected_high,tag;
} m16_fir_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 首次零对象；仅匹配V6，要求共享运行器已开启仲裁。2608字是含保护区的最小arena。
 * 每块最多32个输出；每次最多64个输入/输出检查或复制，不轮询等待硬件。 */
int m16_fir_task_init(m16_fir_task *,m16_projection_runtime *,unsigned,unsigned);
int m16_fir_task_make_provider(m16_fir_task *,m16_fir_provider *);
int m16_fir_task_request(m16_fir_task *,const void *,const m16_fir_request *);
int m16_fir_task_service(m16_fir_task *);
int m16_fir_task_poll(m16_fir_task *,const void *);
int m16_fir_task_cancel(m16_fir_task *,const void *);
int m16_fir_task_owns(const m16_fir_task *,const void *);
#ifdef __cplusplus
}
#endif
#endif
