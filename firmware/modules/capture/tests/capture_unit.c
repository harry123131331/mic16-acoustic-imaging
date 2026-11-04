/* 使用脚本化MMIO和时间回调；不访问硬件，也不执行声学推理。 */
#include "mic16_capture.h"
extern int printf(const char *,...);
typedef struct {
    unsigned int sr,length,tick,snapshots,prepares,submits,starts,stops,resets,consumes,acks;
    int submit_rc,reset_ok,guard_ok;
} fake;
static unsigned int checks,errors;
static void check(int p){checks++;if(!p)errors++;}
static void prepare(void *p){((fake *)p)->prepares++;}
static int submit(void *p,unsigned int n){fake *f=p;f->submits++;check(n==8192);return f->submit_rc;}
static void start(void *p){((fake *)p)->starts++;}
static void snapshot(void *p,mic16_rx_regs *r){fake *f=p;f->snapshots++;*r=(mic16_rx_regs){f->snapshots==1?1u:f->sr,0x15003u,0x123400u,f->length};}
static unsigned long long ticks(void *p){return ((fake *)p)->tick++;}
static void stop(void *p){((fake *)p)->stops++;}
static int reset(void *p){fake *f=p;f->resets++;return f->reset_ok;}
static int consume(void *p,unsigned int n){fake *f=p;f->consumes++;check(n==1024);return f->guard_ok;}
static void ack(void *p){((fake *)p)->acks++;}
static void run(unsigned int which) {
    fake f={.sr=0};mic16_rx_state state={0,0};mic16_rx_report r;int rc;
    mic16_rx_ops ops={&f,prepare,submit,start,snapshot,ticks,stop,reset,consume,ack};
    f.sr=0x1002;f.length=1024;f.reset_ok=1;f.guard_ok=1;
    if(which==1)f.sr=0x5011;
    if(which==2)f.sr=0;
    if(which==3)f.length=2048;
    if(which==4)f.sr=0x1000;
    if(which==5)f.submit_rc=15;
    if(which==6){f.sr=0x5011;f.reset_ok=0;}
    if(which==7)f.guard_ok=0;
    if(which==8)f.length=9000;
    if(which==9)f.length=0;
    if(which==10)f.sr=0x1022;
    rc=mic16_receive_one(&state,&ops,8192,1024,4,&r);
    if(which==0)check(rc==0 && r.code==0 && f.consumes==1 && f.acks==1 && f.resets==0 && state.active==0);
    else {
        unsigned int expected=which==1||which==6||which==10?M16_RX_DMA_ERROR:
            which==2||which==4?M16_RX_TIMEOUT:which==5?M16_RX_SUBMIT:which==7?M16_RX_GUARD:M16_RX_LENGTH;
        check(rc==-(int)expected && r.code==expected && f.stops==1 && f.resets==1 && !f.acks);
        check(f.consumes==(which==7?1u:0u));
        if(which==1||which==6)check(r.last.sr==0x5011);
        if(which==3||which==8||which==9)check(r.actual==f.length);
        if(which==6) {
            unsigned int n=f.prepares,s=f.snapshots;
            check(state.quarantined==1 && state.active==1 && r.reset_ok==0);
            rc=mic16_receive_one(&state,&ops,8192,1024,4,&r);
            check(rc==-M16_RX_QUARANTINE && f.prepares==n && f.snapshots==s && f.submits==1);
        }
    }
    printf("RX_HOST_CASE id=%u errors=%u\n",which,errors);
}
int main(void) {
    unsigned int i;mic16_rx_state s={0,0};mic16_rx_report r;mic16_rx_ops bad={.context=0};
    for(i=0;i<11;i++)run(i);
    check(mic16_receive_one(&s,&bad,8192,1024,4,&r)==-M16_RX_INVALID);
    printf("RX_HOST_RESULT cases=11 checks=%u errors=%u\n",checks,errors);
    return errors?1:0;
}
