#ifndef MIC16_GRAPH_BENCHMARK_H
#define MIC16_GRAPH_BENCHMARK_H
#include "mic16_audio_pipeline_task.h"
#include "mic16_shared_compute_graph.h"
#include "mic16_front_context.h"

/* 固定数字输入诊断，不启动DMA，不替代正式常驻调度或端到端实时验收。
 * 首次完整运行单列，随后关闭/打开/打开/关闭计时；每次使用新epoch。 */
#define M16_GB_RUNS 5u
#define M16_GB_BINS 128u
#define M16_GB_SAMPLES 64u
enum {M16_GB_NEW=0,M16_GB_START=1,M16_GB_FRONT=2,M16_GB_SNAPSHOT=3,M16_GB_PIPELINE=4,
 M16_GB_RETIRE=5,M16_GB_FLUSH=6,M16_GB_STOP_FRONT=7,M16_GB_DONE=8,M16_GB_DRAIN=9,M16_GB_FAILED=10};
enum {M16_GB_EVENT_LOCATION=1,M16_GB_EVENT_AUDIO=2,M16_GB_EVENT_FINISH=3,M16_GB_EVENT_RUN=4};
enum {M16_GB_INVALID=-2100,M16_GB_SEQUENCE=-2101,M16_GB_CRC=-2102,M16_GB_TIMEOUT=-2103};
typedef struct {uint64_t calls,ticks,maximum;} m16_graph_benchmark_bin;
/* observed_device含控制服务的观察间隔，不能冒充硬件精确周期；单位与runtime IO一致。 */
/* 设备间隔是PS提交到观察完成的墙钟上界；不可与CPU时间相加。 */
typedef struct {uint64_t transactions,jobs,prepare,observed_device,collect;
 uint64_t input_words,coefficient_words,output_words,flush_calls,invalidate_calls;
 uint64_t device_polls,last_not_done,completion_bracket,queue_waits,queue_ticks;
 uint64_t flush_cpu,invalidate_cpu,flush_bytes,invalidate_bytes;
} m16_graph_benchmark_mode;
typedef struct {uint64_t calls,ticks,squares,maximum;} m16_graph_benchmark_sample;
typedef struct {
 uint32_t instrumented,error,crc,candidates,sources,input_frames,locations,audio_chunks;
 uint64_t started,front_ticks,snapshot_ticks,detection_ticks,normal_ticks,flush_ticks,total_ticks,steps;
 uint64_t audio_values,FFT_jobs,FIR_jobs,range_fallbacks;
 m16_graph_benchmark_bin cpu[M16_GB_BINS];
 m16_graph_benchmark_sample sample[M16_GB_SAMPLES];
 m16_graph_benchmark_mode mode[6];
 uint64_t FIR_requests,FIR_input_words,FIR_output_words,boundary_reads,sample_roots,sample_clock_reads,queue_clock_reads,profile_clock_errors;
} m16_graph_benchmark_run;
typedef struct {
 uint32_t magic,version,phase,error,done,runs,current,owners_released;
 uint64_t timer_hz,clock_pair_ticks,clock_pair_samples;
 m16_graph_benchmark_run run[M16_GB_RUNS];
} m16_graph_benchmark_report;
typedef struct {
 void *context;
 uint64_t (*ticks)(void *);
 /* 可选观察者仅用于离线原参考逐值比较；ARM计时必须为NULL。 */
 int (*observe)(void *,unsigned,m16_audio_pipeline_task *,const m16_cont_info *,const double *,const double *,unsigned);
 uint64_t timer_hz;
 uint32_t expected_crc;
} m16_graph_benchmark_io;
typedef struct {
 unsigned magic,phase,run,frame,feeding,stop_sent;
 int error;
 uint64_t FFT_before,FIR_before,fallback_before,flush_started;
 uint64_t fir_requests_before,fir_input_before,fir_output_before,boundary_tick,not_done_tick;
 unsigned boundary_bin,boundary_valid,sample_countdown,random_state,sample_active,sample_depth;
 uint64_t sample_start[8],sample_children[8];unsigned sample_id[8];
 m16_graph_benchmark_run *sample_row;
 const void *queued_client[8];uint64_t queued_tick[8];
 const float *input,*reference;
 m16_audio_pipeline_task *pipeline;
 m16_shared_compute_graph *backend;
 m16_front_ring *ring;
 m16_front_context_task *front_context;
 m16_graph_benchmark_report *report;
 m16_graph_benchmark_io io;
 char key[512];
} m16_graph_benchmark;
#ifdef __cplusplus
extern "C" {
#endif
int m16_graph_benchmark_init(m16_graph_benchmark *,m16_graph_benchmark_report *,m16_audio_pipeline_task *,
 m16_shared_compute_graph *,m16_front_ring *,m16_front_context_task *,const float *,const float *,const m16_graph_benchmark_io *);
/* 一个后端有界片段和一个前端/计算图片段；网络服务由外层按正式配额调用。 */
int m16_graph_benchmark_step(m16_graph_benchmark *);
int m16_graph_benchmark_abort(m16_graph_benchmark *,int);
/* 仅供诊断runtime快照在真正COMPLETE边界调用，不改变生产runtime。 */
void m16_graph_benchmark_projection_complete(const m16_projection_runtime *);
void m16_graph_benchmark_external(unsigned,uint64_t);
extern unsigned g_m16_graph_profile_sample,g_m16_graph_profile_enabled;
void m16_graph_benchmark_sample_begin(unsigned);
void m16_graph_benchmark_sample_end(void);
void m16_graph_benchmark_device_poll(const m16_projection_runtime *,unsigned,int);
void m16_graph_benchmark_claim(const m16_projection_runtime *,const void *,int);
void m16_graph_benchmark_cache(const m16_projection_runtime *,unsigned,unsigned,uint64_t,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
