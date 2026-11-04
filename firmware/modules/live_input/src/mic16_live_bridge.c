#include "mic16_live_bridge.h"
int mic16_live_run_with_observer(mic16_live_input *s,mic16_live_binding *binding,mic16_search_memory *memory,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w,mic16_cycle_output *event,
    mic16_pipeline_observer observer,void *context) {
    mic16_live_view v;int rc;
    if(!s || !binding || !memory || !o || !w || !event || !observer)return M16_LIVE_INVALID;
    rc=mic16_live_take(s,&v);if(rc)return rc;
    rc=observer(v.pcm,v.reference,o,w,context);
    if(rc || !mic16_live_view_current(s,&v)) {
        mic16_live_interrupt(s,0);mic16_live_release(s,&v);
        mic16_memory_init(memory);binding->has_epoch=0;return rc ? -10 : M16_LIVE_GAP;
    }
    if(!binding->has_epoch || binding->epoch!=v.epoch)mic16_memory_init(memory);
    rc=mic16_consume_scored_cycle(memory,o->directions,o->valid,o->scores,
        (double)v.end_sample/39062.5,"live-reference-bound-by-epoch",1,event);
    if(rc) {
        mic16_live_interrupt(s,0);mic16_live_release(s,&v);
        mic16_memory_init(memory);binding->has_epoch=0;return -11;
    }
    binding->epoch=v.epoch;binding->has_epoch=1;
    return mic16_live_release(s,&v);
}
static int ordinary_observer(const float *pcm,const float *reference,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w,void *context){
    (void)context;return mic16_pipeline_observe(pcm,reference,o,w);
}
int mic16_live_run(mic16_live_input *s,mic16_live_binding *binding,mic16_search_memory *memory,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w,mic16_cycle_output *event){
    return mic16_live_run_with_observer(s,binding,memory,o,w,event,ordinary_observer,0);
}
