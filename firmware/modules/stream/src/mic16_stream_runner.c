#include "mic16_stream_runner.h"
#include <stddef.h>
#define MAGIC 0x4d53524eu
static int valid(const mic16_stream_runner *r){return r && r->magic==MAGIC;}
static int separate(const void *a,size_t na,const void *b,size_t nb){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-na && y<=UINTPTR_MAX-nb && (x+na<=y || y+nb<=x);
}
static uint64_t now(mic16_stream_runner *r){return r->ops.ticks(r->ops.context);}
static int failed(mic16_stream_runner *r){return r->queue.failure?r->queue.failure:M16_STREAM_STOPPED;}
static int cleanup(mic16_stream_runner *r){
    unsigned i;int stopped;
    if(r->capture_held){
        stopped=r->ops.capture_stop(r->ops.context);
        if(stopped!=1){if(stopped<0)r->backend_error=stopped;return M16_RUNNER_QUIESCING;}
        (void)mic16_stream_input_release(&r->queue,&r->capture);r->capture_held=0;
    }
    /* 仅计算回调返回之后，结果载荷和输入载荷才可收回。 */
    if(r->in_callback)return failed(r);
    if(r->compute_held){(void)mic16_stream_input_release(&r->queue,&r->compute);r->compute_held=0;}
    if(r->send_held){(void)mic16_stream_output_cancel(&r->queue,&r->send);r->send_held=0;}
    for(i=0;i<3;i++)if(r->build[i].pool && r->queue.output[i].state==M16_STREAM_BUILD){
        (void)mic16_stream_output_cancel(&r->queue,r->build+i);
    }
    return failed(r);
}
int mic16_stream_runner_stop(mic16_stream_runner *r,int reason){
    int rc;if(!valid(r))return M16_STREAM_INVALID;
    rc=mic16_stream_abort(&r->queue,reason);
    if(rc==M16_STREAM_INVALID)return rc;
    return cleanup(r);
}
static int backend_failure(mic16_stream_runner *r,int rc,int reason){
    r->backend_error=rc;return mic16_stream_runner_stop(r,reason);
}
int mic16_stream_runner_init(mic16_stream_runner *r,const mic16_stream_runner_config *c,
    const mic16_stream_runner_ops *o,unsigned epoch){
    int rc;
    if(!r || !c || !o || (uintptr_t)r%_Alignof(mic16_stream_runner) || r->magic ||
        !separate(r,sizeof(*r),c,sizeof(*c)) || !separate(r,sizeof(*r),o,sizeof(*o)) ||
        !c->capture_timeout_ticks || !o->ticks || !o->capture_start || !o->capture_poll ||
        !o->capture_stop || !o->compute || !o->finish || !o->send_step)return M16_STREAM_INVALID;
    rc=mic16_stream_init(&r->queue,&c->queue,epoch,o->ticks(o->context));if(rc)return rc;
    r->ops=*o;r->capture_timeout_ticks=c->capture_timeout_ticks;r->input_limit=c->input_limit;r->magic=MAGIC;
    return 0;
}
static int capture_service(mic16_stream_runner *r){
    int rc;uint64_t t=now(r);mic16_stream_window w;
    rc=mic16_stream_poll(&r->queue,t);if(rc)return cleanup(r);
    if(r->capture_held){
        rc=r->ops.capture_poll(r->ops.context);
        if(rc<0 || rc>1)return backend_failure(r,rc,M16_STREAM_INPUT_INVALID);
        if(rc==1){
            /* 后端已结束写访问；即使提交检查失败，也必须在故障后显式归还。 */
            rc=mic16_stream_input_commit(&r->queue,&r->capture,now(r));
            if(rc)return cleanup(r);
            r->capture_held=0;
        }else if(t-r->capture_started>r->capture_timeout_ticks){
            return mic16_stream_runner_stop(r,M16_STREAM_EXPIRED);
        }
    }
    if(!r->capture_held && (!r->input_limit || r->queue.next_input_first/M16_STREAM_WINDOW<r->input_limit)){
        rc=mic16_stream_input_reserve(&r->queue,r->queue.next_input_first,now(r),&r->capture);
        if(rc)return cleanup(r);
        r->capture_held=1;r->capture_started=now(r);
        w=r->queue.input[r->capture.slot].window;
        rc=r->ops.capture_start(r->ops.context,r->capture.slot,&w);
        if(rc)return backend_failure(r,rc,M16_STREAM_INPUT_INVALID);
    }
    return 0;
}
static int send_service(mic16_stream_runner *r){
    int rc;
    if(!r->send_held){
        rc=mic16_stream_output_acquire(&r->queue,now(r),&r->send,&r->send_window);
        if(rc==M16_STREAM_EMPTY || rc==M16_STREAM_BUSY)return 0;
        if(rc)return cleanup(r);
        r->send_held=1;
    }
    rc=mic16_stream_output_current(&r->queue,&r->send,now(r));if(rc)return cleanup(r);
    rc=r->ops.send_step(r->ops.context,r->send.slot,&r->send_window);
    if(rc<0 || rc>1)return backend_failure(r,rc,M16_STREAM_SEND_FAILED);
    if(rc==1){
        rc=mic16_stream_output_finish(&r->queue,&r->send,now(r),1);r->send_held=0;
        if(rc)return cleanup(r);
    }else{
        rc=mic16_stream_poll(&r->queue,now(r));if(rc)return cleanup(r);
    }
    return 0;
}
int mic16_stream_runner_checkpoint(mic16_stream_runner *r){
    int rc;
    if(!valid(r))return M16_STREAM_INVALID;
    if(r->in_checkpoint)return M16_STREAM_BUSY;
    if(r->queue.failure)return cleanup(r);
    if(r->finished)return M16_RUNNER_DONE;
    r->in_checkpoint=1;r->checkpoint_calls++;
    rc=capture_service(r);if(!rc)rc=send_service(r);
    r->in_checkpoint=0;return rc;
}
int mic16_stream_runner_build_slot(mic16_stream_runner *r,uint64_t window,unsigned *slot){
    unsigned i;
    if(!valid(r) || !slot || !separate(r,sizeof(*r),slot,sizeof(*slot)))return M16_STREAM_INVALID;
    if(r->queue.failure)return failed(r);
    for(i=0;i<r->queue.config.output_slots;i++){
        mic16_stream_slot *p=r->queue.output+i;
        if(p->state==M16_STREAM_BUILD && p->window.first_sample/M16_STREAM_WINDOW==window &&
            r->build[i].generation==p->generation){*slot=i;return 0;}
    }
    return M16_STREAM_NO_RESULT;
}
static int commit_emitted(mic16_stream_runner *r,uint64_t count){
    unsigned slot;int rc;
    if(count<r->emitted || count>r->computed)return mic16_stream_runner_stop(r,M16_STREAM_NO_RESULT);
    for(;r->emitted<count;r->emitted++){
        rc=mic16_stream_runner_build_slot(r,r->emitted,&slot);
        if(rc)return mic16_stream_runner_stop(r,M16_STREAM_NO_RESULT);
        rc=mic16_stream_output_commit(&r->queue,r->build+slot,now(r));if(rc)return cleanup(r);
    }
    return 0;
}
static int process_one(mic16_stream_runner *r){
    int rc;uint64_t emitted=r->emitted;mic16_stream_ticket build;
    if(!r->compute_held){
        rc=mic16_stream_input_acquire(&r->queue,now(r),&r->compute,&r->compute_window);
        if(rc==M16_STREAM_EMPTY || rc==M16_STREAM_BUSY)return 0;
        if(rc)return cleanup(r);
        r->compute_held=1;
    }
    rc=mic16_stream_output_reserve(&r->queue,&r->compute,now(r),&build);
    if(rc==M16_STREAM_BUSY)return 0;
    if(rc)return cleanup(r);
    r->build[build.slot]=build;
    r->in_callback=1;
    rc=r->ops.compute(r->ops.context,r,r->compute.slot,&r->compute_window,build.slot,&emitted);
    r->in_callback=0;
    if(r->queue.failure)return cleanup(r);
    if(rc)return backend_failure(r,rc,M16_STREAM_INPUT_INVALID);
    rc=mic16_stream_poll(&r->queue,now(r));if(rc)return cleanup(r);
    rc=mic16_stream_input_release(&r->queue,&r->compute);r->compute_held=0;
    if(rc)return cleanup(r);
    r->computed++;
    return commit_emitted(r,emitted);
}
int mic16_stream_runner_step(mic16_stream_runner *r){
    int rc;uint64_t emitted;
    if(!valid(r))return M16_STREAM_INVALID;
    if(r->in_step || r->in_callback || r->in_checkpoint)return M16_STREAM_BUSY;
    if(r->queue.failure)return cleanup(r);
    if(r->finished)return M16_RUNNER_DONE;
    r->in_step=1;rc=mic16_stream_runner_checkpoint(r);
    if(!rc)rc=process_one(r);
    if(!rc && r->input_limit && r->computed==r->input_limit && !r->finish_called){
        emitted=r->emitted;r->in_callback=1;
        r->finish_called=1;
        rc=r->ops.finish(r->ops.context,r,&emitted);r->in_callback=0;
        if(r->queue.failure)rc=cleanup(r);
        else if(rc)rc=backend_failure(r,rc,M16_STREAM_INPUT_INVALID);
        else if(emitted!=r->computed)rc=mic16_stream_runner_stop(r,M16_STREAM_NO_RESULT);
        else rc=commit_emitted(r,emitted);
    }
    if(!rc)rc=send_service(r);
    if(!rc && r->input_limit && r->queue.stats.outputs_sent==r->input_limit){r->finished=1;rc=M16_RUNNER_DONE;}
    r->in_step=0;return rc;
}
