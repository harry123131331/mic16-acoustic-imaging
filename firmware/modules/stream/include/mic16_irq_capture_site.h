#ifndef MIC16_IRQ_CAPTURE_SITE_H
#define MIC16_IRQ_CAPTURE_SITE_H
/* 新GPIO状态接线未生成匹配BIT/XSA前保持0，不能用旧BIT靠改DDR绕过。
 * GPIO2通道1：bit0～1保留原按钮，bit2=前端粘滞溢出，bit3=PDM时钟运行。 */
#define M16_IRQ_STATUS_WIRING_APPROVED 0u
#define M16_IRQ_WARMUP_PACKETS 512u
#define M16_IRQ_DIAGNOSTIC_WINDOWS 32u
#endif
