#include "../../mic16_cooperate.h"
#include "mic16_network.h"
#include "lwip/init.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "netif/xadapter.h"
#include "xparameters.h"
#include "xparameters_ps.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xtime_l.h"
static struct netif netif;
static struct udp_pcb *pcb;
static ip_addr_t peer;
static unsigned port,spacing;
static XTime last_send;
volatile m16_network_stats g_m16_network_stats;
u32_t sys_now(void){XTime t;XTime_GetTime(&t);return (u32_t)(t/(COUNTS_PER_SECOND/1000u));}
void m16_network_poll(void){if(g_m16_network_stats.initialized){xemacif_input(&netif);sys_check_timeouts();}}
static int arp_ready(void){
    struct eth_addr *mac;const ip4_addr_t *address;XTime start,now;
    if(etharp_find_addr(&netif,ip_2_ip4(&peer),&mac,&address)>=0)return 1;
    etharp_query(&netif,ip_2_ip4(&peer),NULL);XTime_GetTime(&start);
    do{M16_COOPERATE();m16_network_poll();if(etharp_find_addr(&netif,ip_2_ip4(&peer),&mac,&address)>=0)return 1;XTime_GetTime(&now);}while(now-start<3ULL*COUNTS_PER_SECOND);
    g_m16_network_stats.arp_timeouts++;return 0;
}
static int initialize(const m16_network_config *config,unsigned wait_for_peer){
    ip_addr_t board,mask,gateway;unsigned i;
    if(!config || config->verified!=1 || !config->port || config->packet_spacing_us>10000 || g_m16_network_stats.initialized)return -1;
    if(config->mac[0]&1 || !(config->mac[0]|config->mac[1]|config->mac[2]|config->mac[3]|config->mac[4]|config->mac[5]))return -1;
    for(i=0;i<4;i++)if((config->board[i]&config->mask[i])!=(config->peer[i]&config->mask[i]))return -1;
    if(config->peer[0]>=224 || !config->peer[0])return -1;
    IP4_ADDR(&board,config->board[0],config->board[1],config->board[2],config->board[3]);
    IP4_ADDR(&peer,config->peer[0],config->peer[1],config->peer[2],config->peer[3]);
    IP4_ADDR(&mask,config->mask[0],config->mask[1],config->mask[2],config->mask[3]);
    IP4_ADDR(&gateway,config->gateway[0],config->gateway[1],config->gateway[2],config->gateway[3]);
    port=config->port;spacing=config->packet_spacing_us;
    Xil_ExceptionInit();
    if(XScuGic_DeviceInitialize(XPAR_SCUGIC_0_DEVICE_ID)!=0)return -2;
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,(Xil_ExceptionHandler)XScuGic_DeviceInterruptHandler,(void *)XPAR_SCUGIC_0_DEVICE_ID);
    lwip_init();
    if(!xemac_add(&netif,&board,&mask,&gateway,(unsigned char *)config->mac,XPAR_PS7_ETHERNET_0_BASEADDR))return -3;
    netif_set_default(&netif);netif_set_up(&netif);Xil_ExceptionEnable();
    pcb=udp_new();if(!pcb)return -4;
    if(udp_bind(pcb,IP_ADDR_ANY,(u16_t)port)!=ERR_OK){udp_remove(pcb);pcb=0;return -5;}
    g_m16_network_stats.initialized=1;
    if(wait_for_peer && !arp_ready()){g_m16_network_stats.failed=1;return -6;}
    XTime_GetTime(&last_send);return 0;
}
int m16_network_init(const m16_network_config *config){return initialize(config,1);}
/* 新常驻主循环使用此入口：电脑暂未连接也能进入待机，后续发送再非阻塞解析ARP。
 * GEM/中断基础设施仅初始化一次；新DMA调度器不得再次重置同一个GIC。 */
int m16_network_init_resident(const m16_network_config *config){return initialize(config,0);}
int m16_network_send(void *context,const unsigned char *data,unsigned bytes){
    struct pbuf *packet;err_t error;XTime now;(void)context;
    if(!g_m16_network_stats.initialized || g_m16_network_stats.failed || !data || bytes<36 || bytes>1084)return -1;
    if(!arp_ready())goto bad;
    do{M16_COOPERATE();m16_network_poll();XTime_GetTime(&now);}while(now-last_send<(u64)spacing*COUNTS_PER_SECOND/1000000u);
    packet=pbuf_alloc(PBUF_TRANSPORT,(u16_t)bytes,PBUF_RAM);if(!packet)goto bad;
    error=pbuf_take(packet,data,(u16_t)bytes);
    if(error==ERR_OK)error=udp_sendto(pcb,packet,&peer,(u16_t)port);
    pbuf_free(packet);
    if(error!=ERR_OK)goto bad;
    last_send=now;g_m16_network_stats.submitted_packets++;g_m16_network_stats.submitted_bytes+=bytes;
    return 0;
bad:g_m16_network_stats.send_errors++;g_m16_network_stats.failed=1;return -1;
}

int m16_network_try_send_audio(const unsigned char *data,unsigned bytes){
    static XTime query_time;static unsigned queried;
    struct eth_addr *mac;const ip4_addr_t *address;struct pbuf *packet;err_t error;XTime now;
    if(!g_m16_network_stats.initialized||g_m16_network_stats.failed||!pcb||!data||bytes<36||bytes>1084)return -1;
    XTime_GetTime(&now);
    if(now<last_send)return -1;
    if(now-last_send<(u64)spacing*COUNTS_PER_SECOND/1000000u)return 1;
    if(etharp_find_addr(&netif,ip_2_ip4(&peer),&mac,&address)<0){
        if(!queried||now<query_time||now-query_time>=COUNTS_PER_SECOND/4u){
            queried=1;query_time=now;etharp_query(&netif,ip_2_ip4(&peer),NULL);
        }
        return 1;
    }
    packet=pbuf_alloc(PBUF_TRANSPORT,(u16_t)bytes,PBUF_RAM);if(!packet)return 1;
    error=pbuf_take(packet,data,(u16_t)bytes);
    if(error==ERR_OK)error=udp_sendto(pcb,packet,&peer,(u16_t)port);
    pbuf_free(packet);
    if(error==ERR_MEM||error==ERR_BUF||error==ERR_RTE)return 1;
    if(error!=ERR_OK){g_m16_network_stats.send_errors++;return -1;}
    last_send=now;g_m16_network_stats.submitted_packets++;g_m16_network_stats.submitted_bytes+=bytes;
    return 0;
}

/* 独立数据端口：5005原始PCM包为1088字节，其它既有端口仍限制1072字节。
 * 应用层不等待ARP、不忙等包间隔、不改变音频错误状态。
 * 底层lwIP/GEM一次调用的真实耗时仍须实测，不把此函数称作硬实时网口。 */
int m16_network_try_send_to(unsigned remote_port,const unsigned char *data,unsigned bytes){
    static XTime query_time,raw_last_send;static unsigned queried,raw_sent;
    struct eth_addr *mac;const ip4_addr_t *address;struct pbuf *packet;err_t error;XTime now;
    if(!g_m16_network_stats.initialized||g_m16_network_stats.failed||!pcb||!data||
       bytes<72||bytes>(remote_port==5005u?1088u:1072u)||!remote_port||remote_port>65535||remote_port==port)return -1;
    /* PCM每包16个输入样本；400微秒只是非阻塞发包配额，不宣称计算已实时。
     * 返回1时调用方保留本包，继续其它计算及控制服务，不丢包、不忙等。 */
    if(remote_port==5005u){
        XTime_GetTime(&now);
        if(raw_sent&&now<raw_last_send)return -1;
        if(raw_sent&&now-raw_last_send<COUNTS_PER_SECOND/2500u)return 1;
    }
    if(etharp_find_addr(&netif,ip_2_ip4(&peer),&mac,&address)<0){
        XTime_GetTime(&now);
        if(!queried||now-query_time>=COUNTS_PER_SECOND/4u){
            queried=1;query_time=now;etharp_query(&netif,ip_2_ip4(&peer),NULL);
        }
        return 1;
    }
    packet=pbuf_alloc(PBUF_TRANSPORT,(u16_t)bytes,PBUF_RAM);if(!packet)return 1;
    error=pbuf_take(packet,data,(u16_t)bytes);
    if(error==ERR_OK)error=udp_sendto(pcb,packet,&peer,(u16_t)remote_port);
    pbuf_free(packet);
    if(error==ERR_MEM||error==ERR_BUF)return 1;
    if(error==ERR_OK&&remote_port==5005u){raw_last_send=now;raw_sent=1;}
    return error==ERR_OK?0:-1;
}
