#ifndef MIC16_IRQ_RING_H
#define MIC16_IRQ_RING_H
#include <stdint.h>
#define M16_IRQ_SLOTS 3u
#define M16_IRQ_PACKETS 2560u
#define M16_IRQ_BYTES 1024u
#define M16_IRQ_WINDOW 40960u
enum {M16_IRQ_OK=0,M16_IRQ_EMPTY=1,M16_IRQ_DONE=2,M16_IRQ_SPURIOUS=3,
 M16_IRQ_INVALID=-80,M16_IRQ_STOPPED=-81,M16_IRQ_OVERRUN=-82,M16_IRQ_DMA=-83,
 M16_IRQ_LENGTH=-84,M16_IRQ_PL_OVERFLOW=-85,M16_IRQ_CLOCK=-86,M16_IRQ_LEASE=-87,
 M16_IRQ_CANCELLED=-88,M16_IRQ_NOT_WIRED=-89,M16_IRQ_COUNTER=-90,M16_IRQ_GUARD=-91,M16_IRQ_TIMEOUT=-92};
enum {M16_IRQ_FREE=0,M16_IRQ_FILLING=1,M16_IRQ_READY=2,M16_IRQ_READING=3};
typedef struct {unsigned slot;uint64_t generation,window,received_tick;} mic16_irq_lease;
typedef struct {unsigned state;uint64_t generation,window,received_tick;} mic16_irq_slot;
/* ISR和主线程必须在同一CPU上串行访问。主线程操作时由适配层短暂屏蔽IRQ并
 * 保存/恢复原状态；不在此处直接操作缓存或硬件。元数据变化不等于DMA停止。
 * 故障后FILLING槽只有设备确实停止、调用quiesced(1)后才能归还。 */
typedef struct {
 unsigned magic,active,slot,packet,done,quarantined;
 int failure;
 uint64_t generation,limit,completed,taken,packets,last_tick;
 uint64_t overrun_count,spurious,peak_used;
 mic16_irq_slot slots[3];
} mic16_irq_ring;
int mic16_irq_ring_init(mic16_irq_ring *,uint64_t window_limit,uint64_t now);
int mic16_irq_ring_destination(const mic16_irq_ring *,unsigned *slot,unsigned *packet);
/* sr来自同一次S2MM完成快照；必须IOC+IDLE、长度1024、无错误；GPIO bit0
 * 是已解码的粘滞溢出，bit1是已解码时钟运行；硬件GPIO实际位为2、3。
 * 只有这些检查通过，载荷才允许进入原始窗。 */
int mic16_irq_packet_valid(uint32_t sr,unsigned bytes,uint32_t pl_status);
int mic16_irq_ring_complete(mic16_irq_ring *,uint32_t sr,unsigned bytes,uint32_t pl_status,uint64_t now);
int mic16_irq_ring_take(mic16_irq_ring *,mic16_irq_lease *);
int mic16_irq_ring_release(mic16_irq_ring *,const mic16_irq_lease *);
int mic16_irq_ring_abort(mic16_irq_ring *,int reason);
int mic16_irq_ring_quiesced(mic16_irq_ring *,int stopped);
#endif
