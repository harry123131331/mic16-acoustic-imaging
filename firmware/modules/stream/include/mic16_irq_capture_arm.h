#ifndef MIC16_IRQ_CAPTURE_ARM_H
#define MIC16_IRQ_CAPTURE_ARM_H
#include "mic16_irq_ring.h"
#if defined(MIC16_IRQ_CAPTURE_ENABLED) && MIC16_IRQ_CAPTURE_ENABLED
#include "xaxidma.h"
#define M16_IRQ_CAPACITY 8192u
typedef struct {
 uint32_t before[16],payload[2048],after[16];
} mic16_irq_dma_buffer __attribute__((aligned(64)));
typedef struct {
 mic16_irq_ring ring;
 XAxiDma dma;
 unsigned char *raw;
 unsigned bounce,warmup,registered,reset_started,reset_failed,stopped;
 uint64_t reset_tick,irq_count,last_irq_tick,max_irq_gap_ticks,max_handler_ticks;
 uint32_t last_sr,last_actual,last_pl,first_bad_pl;
 mic16_irq_dma_buffer buffers[2];
} mic16_irq_capture_arm __attribute__((aligned(64)));
/* 调用前已有m16_network_init完成GIC初始化，沿用该控制器，不重新初始化GIC。
 * raw只由同一CPU的ISR写入、主线程在租约内读取，DMA只访问两份8192字节缓冲。
 * 不能同时运行旧同步DMA接收器；设备须零初始化，raw必须64字节对齐。 */
int mic16_irq_capture_arm_open(mic16_irq_capture_arm *,unsigned char *raw,unsigned raw_bytes,uint64_t windows);
int mic16_irq_capture_arm_status(mic16_irq_capture_arm *);
int mic16_irq_capture_arm_take(mic16_irq_capture_arm *,mic16_irq_lease *);
const unsigned char *mic16_irq_capture_arm_packet(mic16_irq_capture_arm *,const mic16_irq_lease *,unsigned packet);
int mic16_irq_capture_arm_release(mic16_irq_capture_arm *,const mic16_irq_lease *);
/* 返回1才证明DMA reset完成；0保留隔离状态，调用方不得归还/重用DMA缓冲。 */
int mic16_irq_capture_arm_stop(mic16_irq_capture_arm *);
void mic16_irq_capture_arm_handler(void *);
#endif
#endif
