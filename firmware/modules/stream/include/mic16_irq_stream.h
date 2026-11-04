#ifndef MIC16_IRQ_STREAM_H
#define MIC16_IRQ_STREAM_H
#include "mic16_irq_ring.h"
#include "mic16_stream_scheduler.h"
/* 主线程适配：ISR仅操作原始窗环；运行器仅持有映射租约，不复制整窗。
 * stop只停止设备，不能释放计算回调仍在读取的窗。close须在回调返回后调用。 */
typedef struct {
 void *context;
 int (*open)(void *);
 int (*status)(void *);
 int (*take)(void *,mic16_irq_lease *);
 const unsigned char *(*packet)(void *,const mic16_irq_lease *,unsigned);
 int (*release)(void *,const mic16_irq_lease *);
 int (*stop)(void *);
} mic16_irq_stream_ops;
typedef struct {
 unsigned magic,opened,pending,pending_slot,stopped,held[3];
 int failure;
 uint64_t next_window,pending_window;
 mic16_irq_lease leases[3];
 mic16_irq_stream_ops ops;
} mic16_irq_stream;
int mic16_irq_stream_init(mic16_irq_stream *,const mic16_irq_stream_ops *);
int mic16_irq_stream_begin(mic16_irq_stream *,unsigned,const mic16_stream_window *);
int mic16_irq_stream_poll(mic16_irq_stream *);
int mic16_irq_stream_status(mic16_irq_stream *);
const unsigned char *mic16_irq_stream_packet(mic16_irq_stream *,unsigned,uint64_t,unsigned);
int mic16_irq_stream_release(mic16_irq_stream *,unsigned);
/* 返回1才确认设备停止；不释放任何READING原始窗。 */
int mic16_irq_stream_stop(mic16_irq_stream *);
/* 仅主线程且所有计算回调已返回；返回1才完成停止和全部租约回收。 */
int mic16_irq_stream_close(mic16_irq_stream *);
#endif
