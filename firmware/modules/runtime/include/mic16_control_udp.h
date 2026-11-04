#ifndef MIC16_CONTROL_UDP_H
#define MIC16_CONTROL_UDP_H
#include "mic16_resident_control.h"
#include "lwip/udp.h"

#define M16_CONTROL_PORT 5004u
#define M16_CONTROL_RX_SLOTS 4u
typedef struct {unsigned char data[32];uint16_t port;} m16_control_request;
/* 原始lwIP接口只由同一主循环调用，不在ISR、第二核心或递归回调中使用。
 * 与算法/采集缓冲无关。回调只复制32字节；执行命令与回复发生在step中。 */
typedef struct {
    struct udp_pcb *pcb;
    m16_resident_control *control;
    ip_addr_t peer;
    unsigned initialized,head,count,pending;
    uint16_t reply_port;
    m16_control_request rx[M16_CONTROL_RX_SLOTS];
    unsigned char reply[80];
    uint64_t accepted,rejected,overruns,submitted,backpressure,send_errors;
} m16_control_udp;
/* 网络基础设施、control必须先初始化；零对象首次打开，不能覆盖活动pcb。
 * 仅接受配置的电脑IPv4。控制回复从5004发到该电脑实际发命令的临时端口。 */
int m16_control_udp_open(m16_control_udp *,m16_resident_control *,const unsigned char peer[4]);
/* 每次至多执行一个命令、尝试发送一个回复，不循环等待。
 * 返回0本次已处理/无事、1回复等待重试、负数配置或发送异常；异常后仍保留回复。
 * step不启动算法：调度器领取control动作并以真实硬件所有权更新运行状态。 */
int m16_control_udp_step(m16_control_udp *,uint64_t now_ms);
/* 只允许待机且没有未领取动作时关闭；此函数不会停止DMA/PL。 */
int m16_control_udp_close(m16_control_udp *);
#endif
