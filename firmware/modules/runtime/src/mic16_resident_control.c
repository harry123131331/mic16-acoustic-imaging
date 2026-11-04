#include "mic16_resident_control.h"
#include <string.h>
#define MAGIC 0x4d435452u
static uint16_t get16(const unsigned char *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t get32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const unsigned char *p){return (uint64_t)get32(p)|(uint64_t)get32(p+4)<<32;}
static void put16(unsigned char *p,uint16_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void put64(unsigned char *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static uint32_t crc(const unsigned char *p,unsigned n){
    uint32_t v=0xffffffffu;unsigned i,j;
    for(i=0;i<n;i++){v^=p[i];for(j=0;j<8;j++)v=(v>>1)^(0xedb88320u&(0u-(v&1u)));}
    return ~v;
}
static int valid(const m16_resident_control *s){return s && s->magic==MAGIC;}
size_t m16_control_size(void){return sizeof(m16_resident_control);}
size_t m16_control_alignment(void){return _Alignof(m16_resident_control);}
int m16_control_init(m16_resident_control *s,uint32_t boot,uint32_t caps){
    if(!s || (uintptr_t)s%_Alignof(m16_resident_control) || s->magic || !boot || (caps&~255u))return M16_CTL_INVALID;
    memset(s,0,sizeof(*s));s->magic=MAGIC;s->boot=boot;s->capabilities=caps;
    s->window=s->compute_us=M16_CONTROL_UNKNOWN;return 0;
}
static void reply(const m16_resident_control *s,unsigned op,uint32_t request,uint64_t nonce,
                  int code,uint64_t now,unsigned char *out){
    memcpy(out,"M16R",4);put16(out+4,1);put16(out+6,(uint16_t)op);
    put32(out+8,request);put32(out+12,s->boot);put64(out+16,nonce);
    put32(out+24,s->state);put32(out+28,s->capabilities);put32(out+32,s->epoch);
    put32(out+36,(uint32_t)code);put64(out+40,s->window);put64(out+48,s->compute_us);
    put64(out+56,s->queued_windows);put64(out+64,now);put32(out+72,0);put32(out+76,crc(out,76));
}
int m16_control_handle(m16_resident_control *s,const unsigned char *data,unsigned n,
                      unsigned char *out,unsigned capacity,uint64_t now){
    unsigned op;uint32_t request,boot;uint64_t nonce;int code=0;
    unsigned char input[M16_CONTROL_COMMAND_BYTES];
    if(!valid(s)||!data||!out||n!=32||capacity<80)return M16_CTL_INVALID;
    memcpy(input,data,32);
    op=get16(input+6);request=get32(input+8);boot=get32(input+12);nonce=get64(input+16);
    if(memcmp(input,"M16C",4)||get16(input+4)!=1||op<1||op>4||!request||!nonce||get32(input+24)||get32(input+28)!=crc(input,28)){
        s->rejected++;return M16_CTL_INVALID;
    }
    s->requests++;
    if(now<s->last_now_ms){
        s->state=M16_CTL_FAULT;s->code=M16_CTL_CLOCK;s->action=M16_CTL_STOP;
        reply(s,op,request,nonce,s->code,s->last_now_ms,out);return 80;
    }
    s->last_now_ms=now;
    if((op==M16_CTL_HELLO && boot && boot!=s->boot)||(op!=M16_CTL_HELLO && boot!=s->boot)){
        s->rejected++;reply(s,op,request,nonce,M16_CTL_STALE_BOOT,now,out);return 80;
    }
    if(s->cache_valid && nonce==s->nonce){
        if(request==s->last_request){
            if(!memcmp(input,s->cached_command,32)){
                s->duplicates++;memcpy(out,s->cached_reply,80);return 80;
            }
            s->rejected++;reply(s,op,request,nonce,M16_CTL_REPLAY,now,out);return 80;
        }
        if(request<s->last_request){s->rejected++;reply(s,op,request,nonce,M16_CTL_REPLAY,now,out);return 80;}
    }else if(op!=M16_CTL_HELLO){
        s->rejected++;reply(s,op,request,nonce,M16_CTL_REPLAY,now,out);return 80;
    }
    if(op==M16_CTL_START){
        unsigned required=M16_CTL_CONTINUOUS|M16_CTL_SOURCE_AUDIO;
        if((s->capabilities&required)!=required && (s->capabilities&(M16_CTL_LOCALIZATION|M16_CTL_RAW_PCM))!=(M16_CTL_LOCALIZATION|M16_CTL_RAW_PCM))code=M16_CTL_UNSUPPORTED;
        else if(s->state!=M16_CTL_IDLE || s->action)code=M16_CTL_BUSY;
        else if(s->epoch==UINT32_MAX)code=M16_CTL_COUNTER;
        else{
            s->epoch++;s->state=M16_CTL_STARTING;s->code=0;s->action=M16_CTL_START;
            s->window=s->compute_us=M16_CONTROL_UNKNOWN;s->queued_windows=0;s->starts++;
        }
    }else if(op==M16_CTL_STOP){
        if(s->state==M16_CTL_STARTING||s->state==M16_CTL_RUNNING){
            s->state=M16_CTL_DRAINING;s->action=M16_CTL_STOP;s->stops++;
        }else if(s->state==M16_CTL_FAULT){s->action=M16_CTL_STOP;code=s->code;}
    }
    if(!code)code=s->code;
    reply(s,op,request,nonce,code,now,out);
    s->nonce=nonce;s->last_request=request;s->cache_valid=1;
    memcpy(s->cached_command,input,32);memcpy(s->cached_reply,out,80);return 80;
}
unsigned m16_control_take_action(m16_resident_control *s){
    unsigned action;if(!valid(s))return 0;action=s->action;s->action=0;return action;
}
int m16_control_update(m16_resident_control *s,const m16_control_progress *p){
    if(!valid(s)||!p||p->state>M16_CTL_FAULT||(p->busy&~15u)||p->epoch!=s->epoch)return M16_CTL_INVALID;
    if(p->state==M16_CTL_IDLE && (p->busy||s->action||p->queued_windows))return M16_CTL_NOT_QUIESCENT;
    if(p->state!=s->state && p->state!=M16_CTL_FAULT &&
       !(s->state==M16_CTL_STARTING && p->state==M16_CTL_RUNNING) &&
       !((s->capabilities&M16_CTL_SINGLE_CAPTURE)&&s->state==M16_CTL_RUNNING&&p->state==M16_CTL_DRAINING) &&
       !(s->state==M16_CTL_DRAINING && p->state==M16_CTL_IDLE))return M16_CTL_TRANSITION;
    if(s->window!=M16_CONTROL_UNKNOWN && p->window!=M16_CONTROL_UNKNOWN && p->window<s->window)return M16_CTL_COUNTER;
    if(p->state==M16_CTL_FAULT){if(!p->code)return M16_CTL_INVALID;s->action=M16_CTL_STOP;}
    else if(p->code)return M16_CTL_INVALID;
    s->state=p->state;s->code=p->code;s->window=p->window;
    s->compute_us=p->compute_us;s->queued_windows=p->queued_windows;return 0;
}
