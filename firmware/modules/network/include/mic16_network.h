#ifndef MIC16_NETWORK_H
#define MIC16_NETWORK_H
#include <stdint.h>
/* 硬件试验前填写并核验现场参数；默认开关全部关闭。 */
typedef struct {
    unsigned verified;
    uint8_t board[4],peer[4],mask[4],gateway[4],mac[6];
    uint16_t port;
    unsigned packet_spacing_us;
} m16_network_config;
typedef struct {uint32_t initialized,failed,submitted_packets,submitted_bytes,send_errors,arp_timeouts;} m16_network_stats;
extern volatile m16_network_stats g_m16_network_stats;
int m16_network_init(const m16_network_config *);
/* 仅新常驻主循环调用；不等待电脑ARP，不改变旧入口的初始化语义。 */
int m16_network_init_resident(const m16_network_config *);
void m16_network_poll(void);
int m16_network_send(void *,const unsigned char *,unsigned);
/* 音频0已提交、1反压稍后重试、负数失败；调用方保留当前包，不能跳过反压包。
 * 不在内部循环收包、等待间隔或等待ARP；不持有data指针。 */
int m16_network_try_send_audio(const unsigned char *,unsigned);
/* 0已提交、1稍后重试、负数失败；不持有data指针。目的端口与音频分开。 */
int m16_network_try_send_to(unsigned,const unsigned char *,unsigned);
#endif
