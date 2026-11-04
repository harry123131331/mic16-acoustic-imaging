#include "../../modules/mic16_cooperate.h"
#include "mic16_scanner_hls.h"
#include <stddef.h>
#ifdef M16_SCAN_HOST_TEST
extern void *memset(void *,int,size_t);
#else
#include <string.h>
#endif
extern double cos(double),sin(double);
#define GUARD UINT64_C(0x73b5ac19e0624df8)
#define MASK UINT32_C(0x03ffffff)
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x<na:x-y<nb;
}
int m16_scanner_quantize(float x,int32_t *raw){
    double scaled,remainder;int32_t n;
    if(!raw || x!=x || x< -1.000001f || x>1.000001f)return -1;
    scaled=(double)x*16777216.;n=(int32_t)scaled;remainder=scaled-(double)n;
    /* 最近偶数舍入，和AP_RND_CONV一致；不依赖CPU浮点舍入环境。 */
    if(remainder>.5 || (remainder==.5 && n%2))n++;
    else if(remainder<-.5 || (remainder==-.5 && n%2))n--;
    *raw=n;return 0;
}
uint64_t m16_scanner_pack(int32_t real,int32_t imag){
    return ((uint64_t)((uint32_t)imag&MASK)<<26)|((uint32_t)real&MASK);
}
static int guards(const m16_scanner_workspace *w){
    unsigned i;
    for(i=0;i<8;i++)if(w->arena.before[i]!=GUARD || w->arena.after[i]!=GUARD)return 0;
    for(i=M16_SCAN_WORDS;i<M16_SCAN_PADDED_WORDS;i++)if(w->arena.words[i]!=GUARD)return 0;
    return 1;
}
int m16_scanner_init(m16_scanner_workspace *w,const m16_scanner_io *io){
    unsigned i;int32_t a,b;const float *lags;
    if(!w || !io || !io->read || !io->write || !io->flush || !io->invalidate ||
       !io->ticks || !io->barrier || !io->timeout_ticks || ((uintptr_t)w&63u) ||
       io->arena_address<0x100000u || (io->arena_address&63u) ||
       io->arena_address>0x10000000u-M16_SCAN_PADDED_WORDS*8u ||
       overlap(w,sizeof(*w),io,sizeof(*io)) || w->quarantined)return M16_SCAN_INVALID;
    w->io=*io;w->initialized=0;
    for(i=0;i<8;i++)w->arena.before[i]=w->arena.after[i]=GUARD;
    for(i=M16_SCAN_WORDS;i<M16_SCAN_PADDED_WORDS;i++)w->arena.words[i]=GUARD;
    lags=mic16_spatial_lags();
    for(i=0;i<325u*32u;i++){
        double step=6.283185307179586476925286766559*(double)lags[i]/4096.;
        m16_scanner_quantize((float)cos(32.*step),&a);
        m16_scanner_quantize((float)sin(32.*step),&b);
        w->arena.words[M16_SCAN_PHASE_OFFSET+i]=m16_scanner_pack(a,b);
        m16_scanner_quantize((float)cos(step),&a);
        m16_scanner_quantize((float)sin(step),&b);
        w->arena.words[M16_SCAN_STEP_OFFSET+i]=m16_scanner_pack(a,b);
    }
    io->flush(io->context,&w->arena,sizeof(w->arena));io->barrier(io->context);
    w->initialized=1;return 0;
}
static int isolate(m16_scanner_workspace *w,m16_scanner_report *r,int code){
    w->quarantined=1;r->quarantined=1;return code;
}
int m16_scanner_project(m16_scanner_workspace *w,const float *x,float *maps,m16_scanner_report *r){
    unsigned i,t,polls;int32_t re,im;uint64_t start,begin,now,mark,word;
    m16_scanner_io *io;
    if(!w || !x || !maps || !r || !w->initialized || w->quarantined ||
       overlap(x,M16_SP_PHAT_FLOATS*4ul,maps,M16_SP_MAP_FLOATS*4ul) ||
       overlap(x,M16_SP_PHAT_FLOATS*4ul,w,sizeof(*w)) ||
       overlap(maps,M16_SP_MAP_FLOATS*4ul,w,sizeof(*w)) ||
       overlap(r,sizeof(*r),w,sizeof(*w)) || overlap(r,sizeof(*r),x,M16_SP_PHAT_FLOATS*4ul) ||
       overlap(r,sizeof(*r),maps,M16_SP_MAP_FLOATS*4ul))return M16_SCAN_INVALID;
    for(i=0;i<M16_SP_PHAT_FLOATS;i++)if(x[i]!=x[i] || x[i]<-1.000001f || x[i]>1.000001f)return M16_SCAN_INVALID;
    memset(r,0,sizeof(*r));io=&w->io;start=io->ticks(io->context);
    for(t=0;t<20;t++){
        begin=io->ticks(io->context);
        for(i=0;i<M16_SCAN_OBS_WORDS;i++){
            m16_scanner_quantize(x[(t*M16_SCAN_OBS_WORDS+i)*2],&re);
            m16_scanner_quantize(x[(t*M16_SCAN_OBS_WORDS+i)*2+1],&im);
            w->arena.words[i]=m16_scanner_pack(re,im);
        }
        /* 输出也先写入哨兵并刷新，避免旧脏缓存后来覆盖PL结果。 */
        for(i=M16_SCAN_OUTPUT_OFFSET;i<M16_SCAN_WORDS;i++)w->arena.words[i]=GUARD;
        io->flush(io->context,&w->arena,sizeof(w->arena));io->barrier(io->context);
        now=io->ticks(io->context);if(now<begin)return isolate(w,r,M16_SCAN_CLOCK);
        r->packing_ticks+=now-begin;
        r->last_control=io->read(io->context,0);
        if(!(r->last_control&4u) || (r->last_control&0x81u))return isolate(w,r,M16_SCAN_BUSY);
        io->write(io->context,4,0);io->write(io->context,8,0);
        io->write(io->context,0x10,io->arena_address);io->write(io->context,0x14,0);
        io->write(io->context,0x1c,0);io->write(io->context,0x24,M16_SCAN_PHASE_OFFSET);
        io->write(io->context,0x2c,M16_SCAN_STEP_OFFSET);io->write(io->context,0x34,M16_SCAN_OUTPUT_OFFSET);
        io->barrier(io->context);begin=io->ticks(io->context);io->write(io->context,0,1);
        /* 完成标志会读清；即使已完成，也要确认核空闲后才重用DDR。 */
        for(polls=0;;polls++){
            if((polls&255u)==0)M16_COOPERATE();
            r->last_control=io->read(io->context,0);now=io->ticks(io->context);
            if(now<begin)return isolate(w,r,M16_SCAN_CLOCK);
            if(r->last_control&2u)break;
            if(now-begin>=io->timeout_ticks || polls>=100000000u)return isolate(w,r,M16_SCAN_TIMEOUT);
        }
        mark=now;
        for(polls=0;!(r->last_control&4u);polls++){
            if((polls&255u)==0)M16_COOPERATE();
            r->last_control=io->read(io->context,0);now=io->ticks(io->context);
            if(now<mark)return isolate(w,r,M16_SCAN_CLOCK);
            if(now-begin>=io->timeout_ticks || polls>=100000000u)return isolate(w,r,M16_SCAN_TIMEOUT);
            mark=now;
        }
        r->device_ticks+=now-begin;begin=now;
        io->barrier(io->context);io->invalidate(io->context,&w->arena,sizeof(w->arena));io->barrier(io->context);
        if(!guards(w))return isolate(w,r,M16_SCAN_GUARD);
        for(i=0;i<24u*325u;i++){
            word=w->arena.words[M16_SCAN_OUTPUT_OFFSET+i/2];
            re=(int32_t)(uint32_t)(i%2?word>>32:word);
            if(re< -33554432 || re>33554431)return isolate(w,r,M16_SCAN_GUARD);
            maps[t*24u*325u+i]=(float)re*0x1p-24f;
        }
        now=io->ticks(io->context);if(now<begin)return isolate(w,r,M16_SCAN_CLOCK);
        r->decoding_ticks+=now-begin;r->completed_frames++;
    }
    now=io->ticks(io->context);if(now<start)return isolate(w,r,M16_SCAN_CLOCK);
    r->total_ticks=now-start;return 0;
}
