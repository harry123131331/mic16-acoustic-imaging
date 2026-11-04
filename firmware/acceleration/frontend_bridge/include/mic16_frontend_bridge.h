#ifndef MIC16_FRONTEND_BRIDGE_H
#define MIC16_FRONTEND_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
#include "mic16_spectral_front.h"
#ifdef __cplusplus
extern "C" {
#endif
#define M16_FRONT_PCM_VALUES 32768u
#define M16_FRONT_PCM_WORDS 16384u
#define M16_FRONT_SPECTRUM_WORDS 12912u
/* 纯数据适配，不启动硬件。scale必须由调用者明确给出，不能作为现场幅值验收。
 * PCM为逻辑通道顺序[2048,16]；不量化、不重排通道、不猜测原始有效位。
 * 容量必须恰好匹配；输入和输出不重叠。参数/内容无效返回-1，输出保持原样。 */
int m16_frontend_pack_i32(const int32_t *pcm,size_t values,unsigned scale,
    uint64_t *words,size_t word_count);
/* 只读旧工作区的首12912个复数谱字：[通道,807频点]，低FP32实/高FP32虚。
 * 转为[频点,通道,实虚] FP64并除以2^scale，再用原CPU规则计算功率/PHAT。
 * 不读取旧定点PHAT。成功只说明解析与尺度合同，不说明HLS等价或已上板。 */
int m16_frontend_import_f32_spectrum(const uint64_t *words,size_t word_count,
    unsigned scale,mic16_spectral_frame *out);
#ifdef __cplusplus
}
#endif
#endif
