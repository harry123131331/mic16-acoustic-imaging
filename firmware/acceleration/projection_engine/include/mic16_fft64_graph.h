#ifndef MIC16_FFT64_GRAPH_H
#define MIC16_FFT64_GRAPH_H
#include "mic16_fft64_task.h"
#include "mic16_detection_task.h"
#include "mic16_enh_prediction_task.h"
#include "mic16_continuous_task.h"
#include "mic16_front_ring.h"
/* 一个共享后端服务完整图的全部FP64 FFT客户端；对象常驻DDR，不在START时重建。 */
typedef struct {
 unsigned magic,bound,clients;
 int error;
 uint64_t service_steps;
 m16_fft_provider provider;
 m16_fft64_task task;
} m16_fft64_graph;
#ifdef __cplusplus
extern "C" {
#endif
/* 全部节点已初始化、空闲且尚未绑定；front为NULL仅供保留的整窗夹具。
 * 登记各节点真正持有的旋转表，不使用其它翻译单元的同名静态副本。
 * 任何登记/绑定失败都不允许启动图，不能静默退回未迁移的平台。 */
int m16_fft64_graph_init(m16_fft64_graph *,m16_projection_runtime *,m16_detection_task *,
 m16_enh_prediction_task *,m16_continuous_task *,m16_front_ring *);
/* 常驻循环每步调用，STOP/故障后也继续排空；不发起采集、不发布结果。 */
int m16_fft64_graph_service(void *);
#ifdef __cplusplus
}
#endif
#endif
