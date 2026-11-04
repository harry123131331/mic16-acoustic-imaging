#include "mic16_video_export.h"
#include <stddef.h>
#include <stdint.h>
static void clear_bytes(void *p,size_t n){unsigned char *b=p;while(n--)*b++=0;}
static uint32_t rd(m16_video_device *d,unsigned offset){return d->ops.read(d->ops.context,offset);}
static int run(m16_video_device *d,unsigned offset,uint32_t value){
 uint32_t previous=rd(d,0xd8);
 if(d->ops.command(d->ops.context,offset,value)||rd(d,0xd8)!=previous+1u){d->fault=1;return -1;}
 return 0;
}
int m16_video_device_open(m16_video_device *d,const m16_video_device_ops *ops){
 uint32_t geometry,bytes;m16_video_device_ops saved;
 if(!d||!ops||!ops->read||!ops->command||!ops->copy)return -1;
 saved=*ops;clear_bytes(d,sizeof(*d));d->ops=saved;
 /* 先读旧版本也支持的a0；旧BIT不得直接访问新子页，以免产生总线异常。 */
 if((rd(d,0xa0)>>16)!=8u||rd(d,0xb8)!=0x4d565831u||rd(d,0xd4)!=M16V_RGB565_LE)return -2;
 geometry=rd(d,0xd0);bytes=rd(d,0xcc);d->width=geometry&65535u;d->height=geometry>>16;
 if(!d->width||d->width>800||!d->height||d->height>480||bytes!=d->width*d->height*2u||bytes>0xc0000u)return -3;
 d->bytes=bytes;d->opened=1;return 0;
}
int m16_video_device_acquire(m16_video_device *d){
 uint32_t status,address,token;
 if(!d||!d->opened||d->fault||d->owned)return -1;
 if(run(d,0xb0,1))return -1;
 status=rd(d,0xbc);
 if(!(status&2u))return status==0x10000u?1:-1;
 if(!(status&1u)){d->fault=1;return -2;}
 address=rd(d,0xc8);token=rd(d,0xc0);
 /* 所有权已授予，即使后续描述符异常也不自动复用；必须明确恢复。 */
 d->owned=1;d->token=token;d->sequence=rd(d,0xc4);d->address=address;
 if(!token||(address!=0x10000000u&&address!=0x100c0000u&&address!=0x10180000u)){
  d->fault=1;return -3;
 }
 return 0;
}
int m16_video_device_release(m16_video_device *d){
 uint32_t status;
 if(!d||!d->opened||!d->owned||!d->token||d->fault)return -1;
 if(run(d,0xb4,d->token))return -1;
 status=rd(d,0xbc);
 if(status!=2u){d->fault=1;return -2;}
 d->owned=0;return 0;
}
int m16_video_device_copy(m16_video_device *d,unsigned offset,unsigned char *out,unsigned bytes){
 if(!d||!d->owned||d->fault||!out||!bytes||offset>d->bytes||bytes>d->bytes-offset)return -1;
 return d->ops.copy(d->ops.context,d->address+offset,out,bytes);
}
static int overlap(const void *a,unsigned an,const void *b,unsigned bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 if(x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
 return x<y+bn&&y<x+an;
}
int m16_video_export_init(m16_video_export *s,m16_video_device *d,unsigned char *a,unsigned char *b,unsigned capacity,
 uint32_t epoch,uint32_t mount,uint32_t mode,uint32_t transform,m16_video_try_send send,void *context){
 if(!s||!d||!d->opened||d->owned||d->fault||!a||!b||capacity<d->bytes||!epoch||!mount||!mode||!transform||!send)return -1;
 if(overlap(a,capacity,b,capacity)||overlap(s,sizeof(*s),a,capacity)||overlap(s,sizeof(*s),b,capacity)||
    overlap(s,sizeof(*s),d,sizeof(*d))||overlap(d,sizeof(*d),a,capacity)||overlap(d,sizeof(*d),b,capacity))return -2;
 clear_bytes(s,sizeof(*s));s->device=d;s->slots[0].pixels=a;s->slots[1].pixels=b;s->send=send;s->send_context=context;
 s->epoch=epoch;s->mount_id=mount;s->mode_id=mode;s->transform_id=transform;s->copy_slot=2;return 0;
}
static uint32_t update_crc(uint32_t crc,const unsigned char *p,unsigned n){
 unsigned i,k;for(i=0;i<n;i++){crc^=p[i];for(k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}return crc;
}
static int fail(m16_video_export *s){s->fault=1;return -1;}
int m16_video_export_poll(m16_video_export *s,uint64_t now,int allow_video){
 m16_video_slot *slot;unsigned i,n,written;int result;
 if(!s||!s->device||s->fault)return -1;
 if(s->clock_started&&now<s->last_now)return fail(s);
 s->clock_started=1;s->last_now=now;
 if(s->copy_slot<2){
  slot=&s->slots[s->copy_slot];
  if(now-slot->started_us>400000u){
   if(m16_video_device_release(s->device))return fail(s);
   slot->state=M16_VIDEO_FREE;s->copy_slot=2;s->dropped++;return 1;
  }
  n=s->device->bytes-slot->offset;if(n>M16_VIDEO_STEP_BYTES)n=M16_VIDEO_STEP_BYTES;
  if(m16_video_device_copy(s->device,slot->offset,slot->pixels+slot->offset,n)){
   if(m16_video_device_release(s->device))return fail(s);
   slot->state=M16_VIDEO_FREE;s->copy_slot=2;s->dropped++;return 1;
  }
  slot->offset+=n;
  if(slot->offset==s->device->bytes){
   /* 网络和CRC均在归还后处理，原相机槽不承受发送反压。 */
   if(m16_video_device_release(s->device))return fail(s);
   slot->state=M16_VIDEO_CRC;slot->offset=0;slot->crc=0xffffffffu;s->copy_slot=2;
  }
  return 1;
 }
 for(i=0;i<2;i++)if(s->slots[i].state!=M16_VIDEO_FREE&&now-s->slots[i].started_us>400000u){s->slots[i].state=M16_VIDEO_FREE;s->dropped++;}
 if(allow_video&&now>=s->next_capture_us){
  s->next_capture_us=now>UINT64_MAX-200000u?UINT64_MAX:now+200000u;
  for(i=0;i<2;i++)if(s->slots[i].state==M16_VIDEO_FREE)break;
  if(i<2){
   result=m16_video_device_acquire(s->device);
   if(result<0)return fail(s);
   if(result==1)s->no_frame++;
   else if(s->have_sequence&&s->last_sequence==s->device->sequence){
    if(m16_video_device_release(s->device))return fail(s);
   }else{
    slot=&s->slots[i];slot->state=M16_VIDEO_COPY;slot->offset=0;slot->index=0;slot->started_us=now;
    clear_bytes(&slot->meta,sizeof(slot->meta));slot->meta.epoch=s->epoch;slot->meta.frame_id=s->device->sequence;
    slot->meta.mount_id=s->mount_id;slot->meta.mode_id=s->mode_id;slot->meta.transform_id=s->transform_id;
    slot->meta.width=(uint16_t)s->device->width;slot->meta.height=(uint16_t)s->device->height;slot->meta.format=M16V_RGB565_LE;
    /* 硬件尚未提供已核验的采集时刻，绝不拿CPU完成时间充当采集时间。 */
    s->last_sequence=s->device->sequence;s->have_sequence=1;s->copy_slot=i;s->acquired++;return 1;
   }
  }else s->busy++;
 }
 for(i=0;i<2;i++)if(s->slots[i].state==M16_VIDEO_CRC){
  slot=&s->slots[i];n=s->device->bytes-slot->offset;if(n>M16_VIDEO_STEP_BYTES)n=M16_VIDEO_STEP_BYTES;
  slot->crc=update_crc(slot->crc,slot->pixels+slot->offset,n);slot->offset+=n;
  if(slot->offset==s->device->bytes){slot->meta.frame_crc32=~slot->crc;slot->state=M16_VIDEO_SEND;}
  return 1;
 }
 if(!allow_video)return 0;
 for(i=0;i<2;i++)if(s->slots[i].state==M16_VIDEO_SEND)break;
 if(i==2)return 0;
 if(i==0&&s->slots[1].state==M16_VIDEO_SEND&&s->slots[1].started_us<s->slots[0].started_us)i=1;
 slot=&s->slots[i];written=0;
 if(m16_video_packet(&slot->meta,slot->pixels,s->device->bytes,slot->index,s->packet,sizeof(s->packet),&written))return fail(s);
 result=s->send(s->send_context,s->packet,written);
 if(result>0){s->busy++;return 0;}
 if(result<0){slot->state=M16_VIDEO_FREE;s->dropped++;return 1;}
 s->submitted_packets++;slot->index++;
 if(slot->index==(s->device->bytes+M16V_PAYLOAD-1u)/M16V_PAYLOAD){slot->state=M16_VIDEO_FREE;s->submitted_frames++;}
 return 1;
}
int m16_video_export_cancel(m16_video_export *s){
 unsigned i;if(!s||!s->device)return -1;
 if(s->device->owned&&m16_video_device_release(s->device))return fail(s);
 for(i=0;i<2;i++)s->slots[i].state=M16_VIDEO_FREE;
 s->copy_slot=2;return 0;
}
