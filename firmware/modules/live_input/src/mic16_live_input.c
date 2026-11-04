#include "mic16_live_input.h"
#include <stddef.h>
static int disjoint(const void *a,uintptr_t an,const void *b,uintptr_t bn) {
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    if(!a || !b || !an || !bn || aa>UINTPTR_MAX-an || bb>UINTPTR_MAX-bn)return 0;
    return aa+an<=bb || bb+bn<=aa;
}
static int config_valid(const mic16_live_config *c) {
    unsigned int i,mask=0;
    if(!c || c->scale_exponent>31u)return 0;
    for(i=0;i<16u;i++) {
        unsigned int x=c->logical_to_wire[i];
        if(x>=16u || (mask&(1u<<x)))return 0;
        mask|=1u<<x;
    }
    return 1;
}
static void boundary(mic16_live_input *s,int invalidate) {
    s->mode=M16_LIVE_STOPPED;s->pcm_samples=0;s->sequence_valid=0;
    s->clock_samples=0;s->window_start=0;s->epoch++;s->revision++;
    if(invalidate)s->reference_samples=0;
}
int mic16_live_init(mic16_live_input *s,const mic16_live_config *c,
    float *r,unsigned int rn,float *p,unsigned int pn) {
    mic16_live_config copy;
    if(!s || !config_valid(c) || rn<8192u*16u || pn<40960u*16u ||
       (uintptr_t)r%sizeof(float) || (uintptr_t)p%sizeof(float) ||
       !disjoint(r,8192u*16u*sizeof(float),p,40960u*16u*sizeof(float)) ||
       !disjoint(s,sizeof(*s),r,8192u*16u*sizeof(float)) ||
       !disjoint(s,sizeof(*s),p,40960u*16u*sizeof(float)) ||
       !disjoint(c,sizeof(*c),r,8192u*16u*sizeof(float)) ||
       !disjoint(c,sizeof(*c),p,40960u*16u*sizeof(float)))return M16_LIVE_INVALID;
    copy=*c;s->reference=r;s->pcm=p;s->config=copy;
    s->epoch=0;s->revision=0;s->leased=0;s->leased_revision=0;
    s->reference_samples=0;s->next_sequence=0;boundary(s,1);
    return M16_LIVE_OK;
}
int mic16_live_configure(mic16_live_input *s,const mic16_live_config *c) {
    if(!s || !config_valid(c))return M16_LIVE_INVALID;
    if(s->leased)return M16_LIVE_BUSY;
    s->config=*c;boundary(s,1);return M16_LIVE_OK;
}
int mic16_live_interrupt(mic16_live_input *s,int invalidate) {
    if(!s || (invalidate!=0 && invalidate!=1))return M16_LIVE_INVALID;
    boundary(s,invalidate);return M16_LIVE_OK;
}
int mic16_live_begin_reference(mic16_live_input *s) {
    if(!s)return M16_LIVE_INVALID;
    if(s->leased)return M16_LIVE_BUSY;
    boundary(s,1);s->mode=M16_LIVE_REFERENCE;return M16_LIVE_OK;
}
int mic16_live_begin_audio(mic16_live_input *s) {
    if(!s)return M16_LIVE_INVALID;
    if(s->leased)return M16_LIVE_BUSY;
    if(s->reference_samples!=8192u)return M16_LIVE_STATE;
    boundary(s,0);s->mode=M16_LIVE_AUDIO;return M16_LIVE_OK;
}
static double decode(const unsigned char *b) {
    uint32_t u=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
    /* 避免无符号转有符号的实现相关行为，包含INT_MIN边界。 */
    return u<=0x7fffffffu ? (double)u : (double)u-4294967296.0;
}
int mic16_live_push(mic16_live_input *s,const unsigned char *b,unsigned int bytes,
    uint32_t seq,int transport_ok) {
    unsigned int i,ch,n;float *dst;double divisor;
    if(!s)return M16_LIVE_INVALID;
    if(s->leased || s->mode==M16_LIVE_READY)return M16_LIVE_BUSY;
    if(s->mode!=M16_LIVE_REFERENCE && s->mode!=M16_LIVE_AUDIO)return M16_LIVE_STATE;
    if(!b || bytes!=1024u || transport_ok!=1 ||
       !disjoint(b,1024u,s->reference,8192u*16u*sizeof(float)) ||
       !disjoint(b,1024u,s->pcm,40960u*16u*sizeof(float)) ||
       !disjoint(b,1024u,s,sizeof(*s))) {
        boundary(s,s->mode==M16_LIVE_REFERENCE);return M16_LIVE_INVALID;
    }
    if(s->sequence_valid && seq!=s->next_sequence) {
        boundary(s,s->mode==M16_LIVE_REFERENCE);return M16_LIVE_GAP;
    }
    divisor=(double)((uint64_t)1u<<s->config.scale_exponent);
    /* 写入任何数组前验证全部样本，不静默裁剪。 */
    for(i=0;i<256u;i++)if(decode(b+4u*i)>divisor || decode(b+4u*i)< -divisor) {
        boundary(s,s->mode==M16_LIVE_REFERENCE);return M16_LIVE_RANGE;
    }
    n=s->mode==M16_LIVE_REFERENCE ? s->reference_samples : s->pcm_samples;
    dst=(s->mode==M16_LIVE_REFERENCE ? s->reference : s->pcm)+n*16u;
    for(i=0;i<16u;i++)for(ch=0;ch<16u;ch++)
        dst[i*16u+ch]=(float)(decode(b+4u*(i*16u+s->config.logical_to_wire[ch]))/divisor);
    s->next_sequence=seq+1u;s->sequence_valid=1;s->clock_samples+=16u;
    if(s->mode==M16_LIVE_REFERENCE) {
        s->reference_samples+=16u;
        if(s->reference_samples==8192u) { s->mode=M16_LIVE_STOPPED;return M16_LIVE_REFERENCE_READY; }
    } else {
        s->pcm_samples+=16u;
        if(s->pcm_samples==40960u) { s->mode=M16_LIVE_READY;return M16_LIVE_WINDOW_READY; }
    }
    return M16_LIVE_OK;
}
int mic16_live_take(mic16_live_input *s,mic16_live_view *v) {
    if(!s || !v)return M16_LIVE_INVALID;
    if(s->leased)return M16_LIVE_BUSY;
    if(s->mode!=M16_LIVE_READY)return M16_LIVE_STATE;
    s->leased=1;s->leased_revision=s->revision;
    v->reference=s->reference;v->pcm=s->pcm;v->epoch=s->epoch;
    v->revision=s->revision;v->context_id=s->config.context_id;
    v->start_sample=s->window_start;v->end_sample=s->clock_samples;
    return M16_LIVE_OK;
}
int mic16_live_view_current(const mic16_live_input *s,const mic16_live_view *v) {
    return s && v && s->leased && s->mode==M16_LIVE_READY &&
        v->revision==s->revision && v->revision==s->leased_revision &&
        v->epoch==s->epoch && v->context_id==s->config.context_id &&
        v->pcm==s->pcm && v->reference==s->reference &&
        v->start_sample==s->window_start && v->end_sample==s->clock_samples;
}
int mic16_live_release(mic16_live_input *s,const mic16_live_view *v) {
    int current;
    if(!s || !v || !s->leased || v->revision!=s->leased_revision ||
       v->pcm!=s->pcm || v->reference!=s->reference)return M16_LIVE_INVALID;
    current=mic16_live_view_current(s,v);s->leased=0;
    if(current) {
        s->pcm_samples=0;s->window_start=s->clock_samples;
        s->revision++;s->mode=M16_LIVE_AUDIO;
    }
    return current ? M16_LIVE_OK : M16_LIVE_GAP;
}
