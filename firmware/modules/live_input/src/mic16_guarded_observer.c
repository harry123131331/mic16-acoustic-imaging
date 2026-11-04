#include "mic16_input_guard.h"
#include <stdint.h>
static int disjoint(const void *a,uintptr_t na,const void *b,uintptr_t nb){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!a || !b || !na || !nb || x>UINTPTR_MAX-na || y>UINTPTR_MAX-nb)return 0;
    return x+na<=y || y+nb<=x;
}
int mic16_observe_with_input_guard(const float *pcm,const float *reference,
    mic16_pipeline_observation *observation,mic16_pipeline_workspace *workspace,void *context){
    mic16_guarded_observer_context *g=context;int rc;
    if(!pcm || !reference || !observation || !workspace || !g ||
        (uintptr_t)g%_Alignof(mic16_guarded_observer_context) ||
        !disjoint(g,sizeof(*g),pcm,40960u*16u*4u) ||
        !disjoint(g,sizeof(*g),reference,8192u*16u*4u) ||
        !disjoint(g,sizeof(*g),observation,sizeof(*observation)) ||
        !disjoint(g,sizeof(*g),workspace,sizeof(*workspace)) ||
        !g->downstream || g->downstream==mic16_observe_with_input_guard)return M16_INPUT_ARGUMENT;
    g->report=(mic16_input_report){M16_INPUT_UNCHECKED,0,0,0,0xffffffffu,0xffffffffu,0};
    rc=mic16_input_inspect(pcm,reference,&g->report);
    if(rc==M16_INPUT_ARGUMENT)g->report.status=rc;
    if(rc)return rc;
    return g->downstream(pcm,reference,observation,workspace,g->downstream_context);
}
