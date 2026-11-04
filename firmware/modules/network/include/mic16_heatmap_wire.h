#ifndef MIC16_HEATMAP_WIRE_H
#define MIC16_HEATMAP_WIRE_H
#include "mic16_source_wire.h"
/* M16H固定13×25方位网格；原480张空间谱等权平均，非模型分数或声压级。
 * 每窗两包、独立UDP5003；展示失败不改变检测或每源音频。
 * 调用方独占工作区；maps必须来自本窗成功完成的空间扫描。 */
typedef struct {double sums[325];float values[325];unsigned char packet[1032];} m16_heatmap_workspace;
int m16_heatmap_encode(uint32_t epoch,uint32_t window,const float *maps,
 m16_heatmap_workspace *,m16_source_datagram_sink,void *);
#endif
