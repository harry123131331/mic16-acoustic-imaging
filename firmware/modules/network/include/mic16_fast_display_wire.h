#ifndef MIC16_FAST_DISPLAY_WIRE_H
#define MIC16_FAST_DISPLAY_WIRE_H
#include "mic16_fast_doa_task.h"
#define M16_FDW_HEADER 96u
#define M16_FDW_MAX_BYTES 1056u
/* M16D v2方向和M16H v2分片共享身份与采样区间；旧M16L/M16H v1保持原语义。 */
int m16_fast_display_encode(const m16_fast_doa_result *,uint32_t boot,uint64_t tick_hz,
 unsigned part,unsigned char *out,unsigned capacity,unsigned *bytes);
#endif
