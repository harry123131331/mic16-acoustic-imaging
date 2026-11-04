#ifndef MIC16_SHARED_COMPUTE_GRAPH_H
#define MIC16_SHARED_COMPUTE_GRAPH_H
#include "mic16_fft64_graph.h"
#include "mic16_fir_task.h"
/* 统一V6后端：原7个FP64 FFT客户端与3个FIR客户端共享同一arena和核。
 * 常驻循环在运行、停止和故障排空时都调用service；不能把CPU标记FREE当成硬件已停止。 */
typedef struct {
 unsigned magic,bound,fir_clients;
 int error;
 uint64_t service_steps;
 m16_fft64_graph fft;
 m16_fir_task fir;
 m16_fir_provider fir_provider;
} m16_shared_compute_graph;
#ifdef __cplusplus
extern "C" {
#endif
int m16_shared_compute_graph_init(m16_shared_compute_graph *,m16_projection_runtime *,m16_detection_task *,
 m16_enh_prediction_task *,m16_continuous_task *,m16_front_ring *);
int m16_shared_compute_graph_service(void *);
#ifdef __cplusplus
}
#endif
#endif
