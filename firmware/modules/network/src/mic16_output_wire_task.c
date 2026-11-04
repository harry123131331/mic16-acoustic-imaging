#include "mic16_output_wire_task.h"
#include <string.h>
#define AUDIO_MAGIC 0x4d314157u
#define MAP_MAGIC 0x4d314857u
typedef char ieee_sizes_required[(sizeof(float)==4 && sizeof(double)==8)?1:-1];
static void u16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void u32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void u64(unsigned char *p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(unsigned char)(v>>(8*i));}
static void f32(unsigned char *p,float v){union{float f;uint32_t u;} x;x.f=v;u32(p,x.u);}
static uint32_t crc(uint32_t c,const unsigned char *p,unsigned n){
 unsigned i,j;for(i=0;i<n;i++){c^=p[i];for(j=0;j<8;j++)c=(c>>1)^((c&1u)?0xedb88320u:0u);}return c;
}
static int overlap(const void *a,uintptr_t na,const void *b,uintptr_t nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;
}
static int reusable(unsigned phase){return phase==M16_OW_IDLE||phase==M16_OW_COMPLETE||phase==M16_OW_STOPPED||phase==M16_OW_FAILED;}
static int epoch_ok(unsigned phase,uint32_t old,uint32_t next){
 return next && next>=old && ((phase!=M16_OW_STOPPED && phase!=M16_OW_FAILED)||next>old);
}
static int ticket_ok(const m16_wire_ticket *p,const void *owner,uint64_t serial,uint32_t epoch,uint32_t window,uint32_t source){
 return p && p->owner==(uintptr_t)owner && p->serial==serial && p->epoch==epoch && p->window==window && p->source_id==source;
}
static void ticket_set(m16_wire_ticket *p,const void *owner,uint64_t serial,uint32_t epoch,uint32_t window,uint32_t source){
 p->owner=(uintptr_t)owner;p->serial=serial;p->epoch=epoch;p->window=window;p->source_id=source;
}
static int audio_fail(m16_audio_wire_task *t,int why){t->error=why;t->phase=M16_OW_FAILED;t->interleaved=0;return why;}
static int map_fail(m16_heatmap_wire_task *t,int why){t->error=why;t->phase=M16_OW_FAILED;t->maps=0;return why;}

int m16_audio_wire_task_init(m16_audio_wire_task *t){
 if(!t||t->magic)return M16_OW_INVALID;
 memset(t,0,sizeof(*t));t->magic=AUDIO_MAGIC;return 0;
}
int m16_audio_wire_task_begin(m16_audio_wire_task *t,const m16_cont_info *info,const float *data,unsigned samples){
 uint64_t first,last;m16_cont_info copy;
 if(!t||t->magic!=AUDIO_MAGIC||!info)return M16_OW_INVALID;
 if(!reusable(t->phase))return M16_OW_BUSY;
 copy=*info;
 if(!epoch_ok(t->phase,t->info.epoch,copy.epoch)||copy.stream_count>24||copy.current_confirmed>1||copy.final>1)return M16_OW_INPUT;
 first=((uint64_t)copy.window*40960u*256u+624u)/625u;
 last=(((uint64_t)copy.window+1u)*40960u*256u+624u)/625u;
 if(copy.output_start!=first||copy.output_end!=last||samples!=last-first)return M16_OW_INPUT;
 if(!copy.source_id){
  if(copy.current_confirmed||copy.ordinal!=copy.stream_count)return M16_OW_INPUT;
 }else if(!copy.stream_count||copy.ordinal>=copy.stream_count||!data||
   overlap(data,(uintptr_t)samples*8u,t,sizeof(*t)))return M16_OW_INPUT;
 t->info=copy;t->samples=samples;t->interleaved=copy.source_id?data:0;
 t->index=t->offset=t->count=t->size=0;t->error=0;
 t->phase=copy.source_id?M16_OW_VALIDATE:(copy.stream_count?M16_OW_COMPLETE:M16_OW_HEADER);
 return t->phase==M16_OW_COMPLETE?M16_OW_DONE:0;
}
int m16_audio_wire_task_step(m16_audio_wire_task *t){
 unsigned i,n;unsigned char *p;
 if(!t||t->magic!=AUDIO_MAGIC)return M16_OW_INVALID;
 p=t->packet;
 switch(t->phase){
 case M16_OW_VALIDATE:
  n=t->samples-t->index;if(n>32)n=32;
  for(i=0;i<n;i++){
   float a=t->interleaved[2u*(t->index+i)],b=t->interleaved[2u*(t->index+i)+1u];
   if(!(a>=-3.402823466e38f&&a<=3.402823466e38f&&b>=-3.402823466e38f&&b<=3.402823466e38f))return audio_fail(t,M16_OW_INPUT);
  }
  t->index+=n;if(t->index==t->samples){t->phase=M16_OW_HEADER;t->index=0;}return 0;
 case M16_OW_HEADER:
  if(t->serial==UINT64_MAX)return audio_fail(t,M16_OW_COUNTER);
  ++t->serial;t->count=t->info.source_id?t->samples-t->offset:0;if(t->count>128)t->count=128;
  t->size=60u+t->count*8u;
  p[0]='M';p[1]='1';p[2]='6';p[3]='S';u16(p+4,1);
  u16(p+6,t->info.current_confirmed|(t->info.final<<1)|(!t->info.source_id?4u:0u));
  u32(p+8,t->info.epoch);u32(p+12,t->info.window);u32(p+16,t->info.source_id);
  u16(p+20,t->info.source_id?t->info.ordinal:0);u16(p+22,t->info.stream_count);
  u64(p+24,t->info.output_start);u64(p+32,t->info.output_end);u32(p+40,t->offset);u32(p+44,t->samples);u32(p+48,16000);
  u16(p+52,t->count);u16(p+54,0);t->index=0;t->phase=M16_OW_PAYLOAD;return 0;
 case M16_OW_PAYLOAD:
  n=t->count-t->index;if(n>32)n=32;
  for(i=0;i<n;i++){
   unsigned j=t->index+i;f32(p+60u+j*8u,t->interleaved[2u*(t->offset+j)]);
   f32(p+64u+j*8u,t->interleaved[2u*(t->offset+j)+1u]);
  }
  t->index+=n;if(t->index==t->count){t->checksum=0xffffffffu;t->index=0;t->phase=M16_OW_CRC_HEADER;}return 0;
 case M16_OW_CRC_HEADER:
  t->checksum=crc(t->checksum,p,56);t->index=0;t->phase=M16_OW_CRC_BODY;return 0;
 case M16_OW_CRC_BODY:
  n=t->count*8u-t->index;if(n>64)n=64;
  t->checksum=crc(t->checksum,p+60u+t->index,n);t->index+=n;
  if(t->index==t->count*8u){u32(p+56,t->checksum^0xffffffffu);t->phase=M16_OW_READY;return M16_OW_PACKET;}return 0;
 case M16_OW_READY:return M16_OW_PACKET;
 case M16_OW_COMPLETE:return M16_OW_DONE;
 case M16_OW_FAILED:return t->error;
 default:return M16_OW_BUSY;
 }
}
int m16_audio_wire_task_packet(const m16_audio_wire_task *t,const unsigned char **p,unsigned *bytes,m16_wire_ticket *ticket){
 if(!t||t->magic!=AUDIO_MAGIC||!p||!bytes||!ticket)return M16_OW_INVALID;
 if(t->phase!=M16_OW_READY)return M16_OW_BUSY;
 *p=t->packet;*bytes=t->size;ticket_set(ticket,t,t->serial,t->info.epoch,t->info.window,t->info.source_id);return 0;
}
int m16_audio_wire_task_ack(m16_audio_wire_task *t,const m16_wire_ticket *ticket){
 if(!t||t->magic!=AUDIO_MAGIC)return M16_OW_INVALID;
 if(t->phase!=M16_OW_READY||!ticket_ok(ticket,t,t->serial,t->info.epoch,t->info.window,t->info.source_id))return M16_OW_LEASE;
 t->offset+=t->count;t->index=0;
 if(!t->info.source_id||t->offset==t->samples){t->phase=M16_OW_COMPLETE;t->interleaved=0;return M16_OW_DONE;}
 t->phase=M16_OW_HEADER;return 0;
}
int m16_audio_wire_task_stop(m16_audio_wire_task *t){
 if(!t||t->magic!=AUDIO_MAGIC)return M16_OW_INVALID;
 t->phase=M16_OW_STOPPED;t->interleaved=0;return 0;
}

int m16_heatmap_wire_task_init(m16_heatmap_wire_task *t){
 if(!t||t->magic)return M16_OW_INVALID;
 memset(t,0,sizeof(*t));t->magic=MAP_MAGIC;return 0;
}
int m16_heatmap_wire_task_begin(m16_heatmap_wire_task *t,uint32_t epoch,uint32_t window,const float *maps){
 if(!t||t->magic!=MAP_MAGIC)return M16_OW_INVALID;
 if(!reusable(t->phase))return M16_OW_BUSY;
 if(!epoch_ok(t->phase,t->epoch,epoch)||!maps||overlap(maps,480u*325u*4u,t,sizeof(*t)))return M16_OW_INPUT;
 t->epoch=epoch;t->window=window;t->maps=maps;t->phase=M16_OW_CLEAR;
 t->index=t->grid=t->offset=t->count=t->size=0;t->error=0;return 0;
}
int m16_heatmap_wire_task_step(m16_heatmap_wire_task *t){
 unsigned i,n;unsigned char *p;
 if(!t||t->magic!=MAP_MAGIC)return M16_OW_INVALID;
 p=t->packet;
 switch(t->phase){
 case M16_OW_CLEAR:
  n=325u-t->index;if(n>64)n=64;for(i=0;i<n;i++)t->sums[t->index+i]=0.;
  t->index+=n;if(t->index==325){t->index=0;t->phase=M16_OW_VALIDATE;}return 0;
 case M16_OW_VALIDATE:
  n=480u*325u-t->index;if(n>64)n=64;
  for(i=0;i<n;i++){
   float value=t->maps[t->index+i];if(!(value>=-2.f&&value<=2.f))return map_fail(t,M16_OW_INPUT);
   t->sums[t->grid]+=(double)value;if(++t->grid==325)t->grid=0;
  }
  t->index+=n;if(t->index==480u*325u){t->index=0;t->phase=M16_OW_MEAN;}return 0;
 case M16_OW_MEAN:
  n=325u-t->index;if(n>64)n=64;
  for(i=0;i<n;i++)t->values[t->index+i]=(float)(t->sums[t->index+i]/480.);
  t->index+=n;if(t->index==325){t->maps=0;t->index=0;t->phase=M16_OW_HEADER;}return 0;
 case M16_OW_HEADER:
  if(t->serial==UINT64_MAX)return map_fail(t,M16_OW_COUNTER);
  ++t->serial;t->count=325u-t->offset;if(t->count>240)t->count=240;t->size=72u+t->count*4u;
  p[0]='M';p[1]='1';p[2]='6';p[3]='H';u16(p+4,1);u16(p+6,72);
  u32(p+8,t->epoch);u32(p+12,t->window);u64(p+16,(uint64_t)t->window*40960u);u64(p+24,((uint64_t)t->window+1u)*40960u);
  u16(p+32,13);u16(p+34,25);u16(p+36,1);u16(p+38,1);
  f32(p+40,-60.f);f32(p+44,-30.f);f32(p+48,5.f);f32(p+52,5.f);u32(p+56,1);u32(p+60,t->offset);u32(p+64,t->count);
  t->index=0;t->phase=M16_OW_PAYLOAD;return 0;
 case M16_OW_PAYLOAD:
  n=t->count-t->index;if(n>32)n=32;
  for(i=0;i<n;i++)f32(p+72u+(t->index+i)*4u,t->values[t->offset+t->index+i]);
  t->index+=n;if(t->index==t->count){t->index=0;t->checksum=0xffffffffu;t->phase=M16_OW_CRC_HEADER;}return 0;
 case M16_OW_CRC_HEADER:
  n=68u-t->index;if(n>64)n=64;t->checksum=crc(t->checksum,p+t->index,n);t->index+=n;
  if(t->index==68){t->index=0;t->phase=M16_OW_CRC_BODY;}return 0;
 case M16_OW_CRC_BODY:
  n=t->count*4u-t->index;if(n>64)n=64;t->checksum=crc(t->checksum,p+72u+t->index,n);t->index+=n;
  if(t->index==t->count*4u){u32(p+68,t->checksum^0xffffffffu);t->phase=M16_OW_READY;return M16_OW_PACKET;}return 0;
 case M16_OW_READY:return M16_OW_PACKET;
 case M16_OW_COMPLETE:return M16_OW_DONE;
 case M16_OW_FAILED:return t->error;
 default:return M16_OW_BUSY;
 }
}
int m16_heatmap_wire_task_input_releasable(const m16_heatmap_wire_task *t){return t&&t->magic==MAP_MAGIC&&!t->maps;}
int m16_heatmap_wire_task_packet(const m16_heatmap_wire_task *t,const unsigned char **p,unsigned *bytes,m16_wire_ticket *ticket){
 if(!t||t->magic!=MAP_MAGIC||!p||!bytes||!ticket)return M16_OW_INVALID;
 if(t->phase!=M16_OW_READY)return M16_OW_BUSY;
 *p=t->packet;*bytes=t->size;ticket_set(ticket,t,t->serial,t->epoch,t->window,0);return 0;
}
int m16_heatmap_wire_task_ack(m16_heatmap_wire_task *t,const m16_wire_ticket *ticket){
 if(!t||t->magic!=MAP_MAGIC)return M16_OW_INVALID;
 if(t->phase!=M16_OW_READY||!ticket_ok(ticket,t,t->serial,t->epoch,t->window,0))return M16_OW_LEASE;
 t->offset+=t->count;t->index=0;if(t->offset==325){t->phase=M16_OW_COMPLETE;return M16_OW_DONE;}
 t->phase=M16_OW_HEADER;return 0;
}
int m16_heatmap_wire_task_stop(m16_heatmap_wire_task *t){
 if(!t||t->magic!=MAP_MAGIC)return M16_OW_INVALID;
 t->phase=M16_OW_STOPPED;t->maps=0;return 0;
}
