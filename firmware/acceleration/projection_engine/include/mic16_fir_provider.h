#ifndef MIC16_FIR_PROVIDER_H
#define MIC16_FIR_PROVIDER_H
#include <stdint.h>
/* 原256/625 FIR的完整请求；kind=0读取F32，kind=1读取F64。
 * channels=1为选定一路，channels=16为交错16路；输出始终为交错FP64。
 * 时间轴由父任务维护，本节点不伪造采样序号或结果时间戳。
 * request为0接收、1忙、2安全回CPU；poll为0完成、1继续、2回CPU。
 * owns期间输入、输出、客户端及provider必须保持有效；停止后也须服务后端至owns=0。 */
typedef struct {
 const void *input;
 double *output;
 unsigned kind,samples,stride,channel,channels,capacity;
} m16_fir_request;
typedef struct {
 void *context;
 int (*request)(void *,const void *,const m16_fir_request *);
 int (*poll)(void *,const void *);
 int (*cancel)(void *,const void *);
 int (*owns)(const void *,const void *);
} m16_fir_provider;
#endif
