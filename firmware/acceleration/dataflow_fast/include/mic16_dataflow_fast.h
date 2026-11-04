#ifndef MIC16_DATAFLOW_FAST_H
#define MIC16_DATAFLOW_FAST_H
#include "mic16_scanner_hls.h"
#include "mic16_pipeline.h"
/* 只允许在窗口边界切换。0保留实现；1连续布局扫描；2再启用16路批次重采样。
 * 固定扫描签名缓存只依赖冻结阵列/网格；每窗PCM缓存必须重新清空有效标志。 */
extern unsigned m16_dataflow_mode,m16_dataflow_calls[6];
int m16_dataflow_select(unsigned);
void m16_dataflow_begin(void);
void m16_dataflow_end(void);
void m16_dataflow_clear_signatures(void);
int m16_scanner_refine_dataflow(const float *,float *,mic16_spatial_workspace *,m16_scanner_refinement *);
int m16_resample_dataflow(const float *,unsigned,unsigned,unsigned,double *);
#endif
