/* 仅用于宿主测试；关闭开关用例直接包含真实生产头文件。 */
#if defined(MIC16_IRQ_CAPTURE_GATE_TEST)
#include "../../firmware/modules/stream/include/mic16_irq_capture_site.h"
#else
#define M16_IRQ_STATUS_WIRING_APPROVED 1u
#define M16_IRQ_WARMUP_PACKETS 512u
#endif
