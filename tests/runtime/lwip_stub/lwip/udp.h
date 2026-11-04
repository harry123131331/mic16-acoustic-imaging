#ifndef M16_TEST_LWIP_UDP_H
#define M16_TEST_LWIP_UDP_H
/* 仅供主机单测的lwIP边界替身；真实ARM编译使用原厂lwIP头文件。 */
#include <stdint.h>
typedef uint16_t u16_t;
typedef int err_t;
typedef struct {uint32_t addr;} ip_addr_t;
#define IP4_ADDR(p,a,b,c,d) ((p)->addr=((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(d))
#define ip_addr_cmp(a,b) ((a)->addr==(b)->addr)
#define IP_ADDR_ANY ((const ip_addr_t *)0)
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_BUF -2
#define ERR_RTE -4
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
struct pbuf {struct pbuf *next;void *payload;u16_t len,tot_len;unsigned char storage[80];};
struct udp_pcb;
typedef void (*udp_recv_fn)(void *,struct udp_pcb *,struct pbuf *,const ip_addr_t *,u16_t);
struct udp_pcb {u16_t local_port;udp_recv_fn receive;void *context;};
struct udp_pcb *udp_new(void);
err_t udp_bind(struct udp_pcb *,const ip_addr_t *,u16_t);
void udp_recv(struct udp_pcb *,udp_recv_fn,void *);
void udp_remove(struct udp_pcb *);
err_t udp_sendto(struct udp_pcb *,struct pbuf *,const ip_addr_t *,u16_t);
struct pbuf *pbuf_alloc(int,u16_t,int);
err_t pbuf_take(struct pbuf *,const void *,u16_t);
u16_t pbuf_copy_partial(const struct pbuf *,void *,u16_t,u16_t);
unsigned pbuf_free(struct pbuf *);
#endif
