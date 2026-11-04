#include "mic16_capture.h"
int mic16_receive_one(mic16_rx_state *state,const mic16_rx_ops *ops,unsigned int capacity,
    unsigned int expected,unsigned long long timeout,mic16_rx_report *r) {
    unsigned long long begin;
    if(!state || !ops || !r || !ops->prepare || !ops->submit || !ops->start_source ||
       !ops->snapshot || !ops->ticks || !ops->stop_source || !ops->reset_checked || !ops->consume || !ops->ack ||
       !expected || expected>capacity || capacity>M16_RX_CAPACITY || capacity%4 || !timeout) return -M16_RX_INVALID;
    *r=(mic16_rx_report){.code=0};r->capacity=capacity;r->expected=expected;
    if(state->quarantined || state->active) { r->code=M16_RX_QUARANTINE;r->quarantined=1;return -M16_RX_QUARANTINE; }
    ops->snapshot(ops->context,&r->before);
    begin=ops->ticks(ops->context);
    if(r->before.sr&M16_RX_ERROR_BITS) { r->last=r->before;r->code=M16_RX_DMA_ERROR;goto fail; }
    ops->prepare(ops->context);
    r->submit_rc=ops->submit(ops->context,capacity);
    if(r->submit_rc) { ops->snapshot(ops->context,&r->last);r->code=M16_RX_SUBMIT;goto fail; }
    state->active=1;
    ops->start_source(ops->context);
    for(;;) {
        ops->snapshot(ops->context,&r->last);r->polls++;
        r->elapsed_ticks=ops->ticks(ops->context)-begin;
        /* 即使IOC完成标志同时存在，错误标志也优先，例如曾出现的0x5011。 */
        if(r->last.sr&M16_RX_ERROR_BITS) { r->code=M16_RX_DMA_ERROR;goto fail; }
        if((r->last.sr&(M16_RX_IOC|M16_RX_IDLE))==(M16_RX_IOC|M16_RX_IDLE)) {
            r->actual=r->last.length;
            if(r->actual!=expected || r->actual>capacity || r->actual%4) { r->code=M16_RX_LENGTH;goto fail; }
            if(!ops->consume(ops->context,r->actual)) { r->code=M16_RX_GUARD;goto fail; }
            ops->ack(ops->context);state->active=0;r->code=M16_RX_OK;return 0;
        }
        if(r->elapsed_ticks>=timeout) { r->code=M16_RX_TIMEOUT;goto fail; }
    }
fail:
    /* 先保存故障快照，停止采集后才请求复位；出错或复位失败时，禁止读取、消费或重新填充缓冲区。 */
    ops->stop_source(ops->context);
    r->reset_ok=ops->reset_checked(ops->context)?1u:0u;
    if(r->reset_ok) state->active=0;
    else {state->quarantined=1;r->quarantined=1;}
    return -(int)r->code;
}
