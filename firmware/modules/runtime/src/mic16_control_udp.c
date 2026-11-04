#include "mic16_control_udp.h"
#include "mic16_network.h"
#include <string.h>

static void receive(void *context,struct udp_pcb *pcb,struct pbuf *packet,
                    const ip_addr_t *address,u16_t port){
    m16_control_udp *s=context;unsigned tail;
    if(!packet)return;
    if(!s||!s->initialized||pcb!=s->pcb||!address||!port||
       !ip_addr_cmp(address,&s->peer)||packet->tot_len!=32){
        if(s)s->rejected++;
        pbuf_free(packet);return;
    }
    if(s->count==M16_CONTROL_RX_SLOTS){s->overruns++;pbuf_free(packet);return;}
    tail=(s->head+s->count)%M16_CONTROL_RX_SLOTS;
    if(pbuf_copy_partial(packet,s->rx[tail].data,32,0)!=32){
        s->rejected++;pbuf_free(packet);return;
    }
    s->rx[tail].port=port;s->count++;s->accepted++;
    pbuf_free(packet);
}
int m16_control_udp_open(m16_control_udp *s,m16_resident_control *control,const unsigned char peer[4]){
    struct udp_pcb *pcb;ip_addr_t address;
    if(!s||!control||!peer||s->initialized||s->pcb||!control->magic||!control->boot||
       !g_m16_network_stats.initialized||g_m16_network_stats.failed||!peer[0]||peer[0]>=224)return -1;
    IP4_ADDR(&address,peer[0],peer[1],peer[2],peer[3]);
    pcb=udp_new();if(!pcb)return -2;
    if(udp_bind(pcb,IP_ADDR_ANY,(u16_t)M16_CONTROL_PORT)!=ERR_OK){udp_remove(pcb);return -3;}
    memset(s,0,sizeof(*s));s->pcb=pcb;s->control=control;s->peer=address;s->initialized=1;
    udp_recv(pcb,receive,s);return 0;
}
static int send_reply(m16_control_udp *s){
    struct pbuf *p;err_t error;
    p=pbuf_alloc(PBUF_TRANSPORT,80,PBUF_RAM);
    if(!p){s->backpressure++;return 1;}
    error=pbuf_take(p,s->reply,80);
    if(error==ERR_OK)error=udp_sendto(s->pcb,p,&s->peer,s->reply_port);
    pbuf_free(p);
    if(error==ERR_MEM||error==ERR_BUF||error==ERR_RTE){s->backpressure++;return 1;}
    if(error!=ERR_OK){s->send_errors++;return -4;}
    s->pending=0;s->submitted++;return 0;
}
int m16_control_udp_step(m16_control_udp *s,uint64_t now_ms){
    m16_control_request *request;int rc;
    if(!s||!s->initialized||!s->pcb||!s->control)return -1;
    if(s->pending)return send_reply(s);
    if(!s->count)return 0;
    request=&s->rx[s->head];
    rc=m16_control_handle(s->control,request->data,32,s->reply,80,now_ms);
    s->reply_port=request->port;s->head=(s->head+1)%M16_CONTROL_RX_SLOTS;s->count--;
    if(rc!=80){s->rejected++;return 0;}
    s->pending=1;return send_reply(s);
}
int m16_control_udp_close(m16_control_udp *s){
    if(!s||!s->initialized)return -1;
    if(!s->control||s->control->state!=M16_CTL_IDLE||s->control->action)return -2;
    udp_recv(s->pcb,0,0);udp_remove(s->pcb);s->pcb=0;
    s->initialized=s->count=s->pending=0;return 0;
}
