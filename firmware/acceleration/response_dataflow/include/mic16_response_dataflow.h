#ifndef MIC16_RESPONSE_DATAFLOW_H
#define MIC16_RESPONSE_DATAFLOW_H
#include "mic16_physical.h"
/* 仅在窗口边界切换。原调用前已有完整参数验证；本支路只替换内部响应。
 * 工作区PHAT在统计阶段会被覆盖，严禁跨物理特征调用复用有效位。 */
extern unsigned m16_response_dataflow_mode,m16_response_dataflow_calls[3];
int m16_response_dataflow_select(unsigned mode);
void m16_response_dataflow_begin(void);
void m16_response_dataflow(const float *lags,float *out,mic16_physical_workspace *work);
#endif
