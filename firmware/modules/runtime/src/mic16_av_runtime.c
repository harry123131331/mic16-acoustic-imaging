#include "mic16_av_runtime.h"
static void clear(void *p,unsigned n){unsigned i;unsigned char *s=p;for(i=0;i<n;i++)s[i]=0;}
static int clock_now(m16_av_runtime *r,uint64_t *now){
 *now=r->ops.now_us(r->ops.context);
 if(r->started&&*now<r->last_now){r->fault=1;return -1;}
 if(!r->started){r->started=1;r->credit_time=*now;r->last_checkpoint=*now;r->last_step=*now;}
 r->last_now=*now;return 0;
}
static void replenish(m16_av_runtime *r,uint64_t now){
 uint64_t dt=now-r->credit_time,add;
 /* 限制乘法上界，不积攒长时间空闲后的发送洪峰。不足1字节时保留累计时间。 */
 if(dt>1000000u)dt=1000000u;
 add=dt*r->rate/1000000u;
 if(add){
  if(add<=r->audio_debt){r->audio_debt-=add;add=0;}
  else{add-=r->audio_debt;r->audio_debt=0;}
  r->credits=r->credits+add>r->burst?r->burst:r->credits+add;
  r->credit_time=now;
 }
}
int m16_av_runtime_init(m16_av_runtime *r,m16_video_export *v,const m16_av_runtime_ops *ops,
 unsigned rate,unsigned burst,unsigned interval){
 if(!r||!ops||!ops->now_us||!ops->network_poll||!ops->video_send||rate<1072u||
    rate>100000000u||burst<1084u||burst>1000000u||!interval||interval>1000000u)return -1;
 clear(r,sizeof(*r));r->ops=*ops;r->video=v;r->rate=rate;r->burst=burst;r->interval_us=interval;r->credits=burst;
 if(v){v->send=m16_av_runtime_video_send;v->send_context=r;}
 return 0;
}
void m16_av_runtime_step(void *context){
 m16_av_runtime *r=context;uint64_t now,end,gap;
 if(!r||r->fault)return;
 if(r->active){r->stats.reentries++;return;}
 if(clock_now(r,&now))return;
 gap=now-r->last_checkpoint;r->last_checkpoint=now;r->stats.checkpoints++;
 if(gap>r->stats.max_checkpoint_gap_us)r->stats.max_checkpoint_gap_us=gap;
 /* 音频采集期间连网络轮询也不运行，避免每个小包的轮询时限被抢占。 */
 if(r->paused||now-r->last_step<r->interval_us)return;
 r->last_step=now;replenish(r,now);r->active=1;r->stats.steps++;
 r->ops.network_poll(r->ops.context);
 if(!r->audio_depth&&r->video&&m16_video_export_poll(r->video,now,1)<0)r->fault=1;
 if(!clock_now(r,&end)&&end-now>r->stats.max_service_us)r->stats.max_service_us=end-now;
 r->active=0;
}
int m16_av_runtime_video_send(void *context,const unsigned char *data,unsigned n){
 m16_av_runtime *r=context;uint64_t now;int rc;
 if(!r||!r->active||r->fault||r->paused||r->audio_depth||!data||n<72||n>1072)return -1;
 if(clock_now(r,&now))return -1;
 replenish(r,now);
 if(r->credits<n){r->stats.budget_denied++;return 1;}
 rc=r->ops.video_send(r->ops.context,data,n);
 if(!rc){r->credits-=n;r->stats.video_bytes+=n;}
 return rc;
}
int m16_av_runtime_pause(m16_av_runtime *r){
 unsigned i;
 if(!r||r->active||r->audio_depth)return -1;
 r->paused=1;r->stats.capture_pauses++;
 if(r->video){
  for(i=0;i<2;i++)if(r->video->slots[i].state!=M16_VIDEO_FREE)r->stats.cancelled_frames++;
  if(m16_video_export_cancel(r->video)){r->fault=1;return -1;}
 }
 return r->fault?-1:0;
}
int m16_av_runtime_resume(m16_av_runtime *r){
 uint64_t now;
 if(!r||r->active||r->audio_depth||r->fault||clock_now(r,&now))return -1;
 r->paused=0;r->last_step=now;r->last_checkpoint=now;r->credit_time=now;r->credits=0;
 if(r->video)r->video->next_capture_us=now;
 return 0;
}
int m16_av_runtime_audio_begin(m16_av_runtime *r){
 if(!r||r->active||r->paused||r->audio_depth)return -1;
 r->audio_depth=1;return 0;
}
int m16_av_runtime_audio_end(m16_av_runtime *r,unsigned bytes,int result){
 uint64_t now;
 if(!r||!r->audio_depth||r->active)return -1;
 r->audio_depth=0;
 if(!clock_now(r,&now))replenish(r,now);
 if(!result){
  r->stats.audio_packets++;r->stats.audio_bytes+=bytes;
  if(r->credits>=bytes)r->credits-=bytes;
  else{
   uint64_t deficit=bytes-r->credits;r->credits=0;
   if(UINT64_MAX-r->audio_debt<deficit){r->fault=1;return -1;}
   r->audio_debt+=deficit;
  }
 }
 return result;
}
