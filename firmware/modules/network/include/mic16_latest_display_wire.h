#ifndef MIC16_LATEST_DISPLAY_WIRE_H
#define MIC16_LATEST_DISPLAY_WIRE_H
#include "mic16_fast_display_wire.h"
#ifdef __cplusplus
extern "C" {
#endif
/* v3仍用M16D/M16H及同一CRC；结果序号与真实四帧起点分别传输。v2保持不变。 */
int m16_latest_display_encode(const m16_fast_doa_result *,uint32_t boot,uint64_t tick_hz,
 unsigned part,unsigned char *,unsigned capacity,unsigned *bytes);
#ifdef __cplusplus
}
#endif
#endif
