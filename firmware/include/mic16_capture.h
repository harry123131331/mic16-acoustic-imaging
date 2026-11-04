#ifndef MIC16_CAPTURE_H
#define MIC16_CAPTURE_H
/* 单次S2MM数据包事务，与BSP、RTOS、网络及推理独立。硬件复位并确认DMA静止后才能初始化；调用期间拥有缓冲区，直到IOC且空闲或复位成功。复位前冻结故障快照，被隔离的缓冲区不得复用。 */
#define M16_RX_EXPECTED 1024u
#define M16_RX_CAPACITY 8192u
#define M16_RX_ERROR_BITS 0x00004770u
#define M16_RX_IOC 0x00001000u
#define M16_RX_IDLE 0x00000002u
enum { M16_RX_OK=0,M16_RX_INVALID=1,M16_RX_SUBMIT=2,M16_RX_DMA_ERROR=3,
       M16_RX_TIMEOUT=4,M16_RX_LENGTH=5,M16_RX_GUARD=6,M16_RX_QUARANTINE=7 };
typedef struct { unsigned int sr,cr,destination,length; } mic16_rx_regs;
typedef struct { unsigned int active,quarantined; } mic16_rx_state;
typedef struct {
    unsigned int code,capacity,expected,actual,polls,reset_ok,quarantined;
    int submit_rc;
    unsigned long long elapsed_ticks;
    mic16_rx_regs before,last;
} mic16_rx_report;
typedef struct {
    void *context;
    void (*prepare)(void *); /* 仅CPU拥有缓冲区时填充保护字、检查哨兵并刷新缓存。 */
    int (*submit)(void *,unsigned int);
    void (*start_source)(void *);
    void (*snapshot)(void *,mic16_rx_regs *);
    unsigned long long (*ticks)(void *);
    void (*stop_source)(void *);
    int (*reset_checked)(void *); /* 有超时边界；仅ResetIsDone成立后返回1。 */
    int (*consume)(void *,unsigned int); /* DMA空闲后使缓存失效、检查并复制数据。 */
    void (*ack)(void *);
} mic16_rx_ops;
int mic16_receive_one(mic16_rx_state *,const mic16_rx_ops *,unsigned int capacity,
    unsigned int expected,unsigned long long timeout_ticks,mic16_rx_report *);
#endif
