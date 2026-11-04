#ifndef MIC16_SOURCE_WIRE_H
#define MIC16_SOURCE_WIRE_H
#include "mic16_continuous.h"
#define M16_SOURCE_WIRE_HEADER 60u
#define M16_SOURCE_WIRE_CHUNK 128u
#define M16_SOURCE_WIRE_MAX 1084u
typedef int (*m16_source_datagram_sink)(void *,const unsigned char *,unsigned);
/* 新增M16S第1版格式，魔数与旧MIC16帧不同。本层不发送或绑定UDP，传输回调必须立即消费每包至多1084字节。接收端失败后调用方须放弃当前时段，不自动补静音或缺口。除无源清单外跳过MIC0回调；波束及增强音频用F32小端。 */
int m16_source_encode(const m16_cont_info *,const double *wdas,const double *enhanced,
    unsigned samples,m16_source_datagram_sink,void *context);
#endif
