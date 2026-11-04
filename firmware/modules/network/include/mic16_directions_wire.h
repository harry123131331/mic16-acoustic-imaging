#ifndef MIC16_DIRECTIONS_WIRE_H
#define MIC16_DIRECTIONS_WIRE_H
#include "mic16_frozen.h"
#include "mic16_source_wire.h"
/* M16L第1版只传当前确认检出，使用匿名ID和估计方向；时间是输入PCM相对时钟，不是真实相机标定证据。最大260字节。 */
int m16_directions_encode(uint32_t epoch,uint32_t window,const mic16_cycle_output *,
    m16_source_datagram_sink,void *);
#endif
