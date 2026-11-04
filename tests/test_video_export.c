/* 受控设备/网络验证所有权、故障和预算，不冒充板端DDR或网口实测。 */
#include "mic16_video_export.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *fopen(const char *,const char *);
extern size_t fwrite(const void *,size_t,size_t,void *);
extern int fclose(void *);
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
static unsigned checks,errors;
static void check(int ok){checks++;if(!ok){errors++;printf("CHECK_FAIL %u\n",checks);}}
typedef struct {
 uint32_t version,status,completion,token,seq,address;
 unsigned held,has_frame,read_new,copies,releases,copy_fail,release_fail,send_busy,send_fail,sends;
 unsigned char source[6144];
 void *file;
} fake_device;
static fake_device f;
static m16_video_device device;
static m16_video_export service;
static unsigned char a[6144+32],b[6144+32];
static uint32_t read_reg(void *ctx,unsigned at){
 fake_device *m=ctx;if(at>=0xb0)m->read_new++;
 switch(at){
  case 0xa0:return m->version<<16;
  case 0xb8:return 0x4d565831u;
  case 0xbc:return m->status;
  case 0xc0:return m->token;
  case 0xc4:return m->seq;
  case 0xc8:return m->address;
  case 0xcc:return 6144;
  case 0xd0:return (48u<<16)|64u;
  case 0xd4:return 1;
  case 0xd8:return m->completion;
  default:check(0);return 0;
 }
}
static int command(void *ctx,unsigned at,uint32_t value){
 fake_device *m=ctx;m->completion++;
 if(at==0xb0){
  check(value==1);
  if(m->held)m->status=0x40001u;
  else if(!m->has_frame)m->status=0x10000u;
  else{m->held=1;m->token++;m->status=3;}
 }else if(at==0xb4){
  if(m->held&&m->token==value&&!m->release_fail){m->held=0;m->status=2;m->releases++;memset(m->source,0xcc,sizeof(m->source));}
  else m->status=0x20001u;
 }else check(0);
 return 0;
}
static int copy(void *ctx,uint32_t address,unsigned char *out,unsigned n){
 fake_device *m=ctx;
 check(m->held&&device.owned);check(address>=m->address&&address-m->address+n<=6144);check(n<=M16_VIDEO_STEP_BYTES);
 m->copies++;if(m->copy_fail)return -1;
 memcpy(out,m->source+address-m->address,n);return 0;
}
static int send(void *ctx,const unsigned char *p,unsigned n){
 fake_device *m=ctx;unsigned char size[4];
 check(!m->held&&!device.owned);check(n>=72&&n<=1072);
 if(m->send_busy)return 1;
 if(m->send_fail)return -1;
 m->sends++;
 if(m->file){
  size[0]=(unsigned char)n;size[1]=(unsigned char)(n>>8);size[2]=0;size[3]=0;
  check(fwrite(size,1,4,m->file)==4&&fwrite(p,1,n,m->file)==n);
 }
 return 0;
}
static void frame(uint32_t seq){
 unsigned i;check(!f.held);f.has_frame=1;f.seq=seq;
 for(i=0;i<6144;i++)f.source[i]=(unsigned char)(i*37u+11u);
}
static void fresh(void){
 m16_video_device_ops ops;
 memset(&f,0,sizeof(f));f.version=8;f.address=0x10000000u;
 ops.context=&f;ops.read=read_reg;ops.command=command;ops.copy=copy;
 check(!m16_video_device_open(&device,&ops));
 memset(a,0xa5,sizeof(a));memset(b,0xa5,sizeof(b));
 check(!m16_video_export_init(&service,&device,a,b,6144,19,1,2,4,send,&f));
}
static void step(uint64_t now,int allow){
 uint32_t before=service.submitted_packets;unsigned i,active=0;
 check(m16_video_export_poll(&service,now,allow)>=0);
 check(service.submitted_packets-before<=1);
 for(i=0;i<2;i++)active+=service.slots[i].state!=M16_VIDEO_FREE;
 check(active<=2);for(i=6144;i<sizeof(a);i++)check(a[i]==0xa5&&b[i]==0xa5);
}
int main(int argc,char **argv){
 m16_video_device_ops ops;unsigned i;uint64_t t;
 if(argc!=2)return 2;
 fresh();f.version=7;ops=device.ops;
 check(m16_video_device_open(&device,&ops)==-2&&f.read_new==4); /* fresh读了4个新寄存器 */
 fresh();check(m16_video_device_acquire(&device)==1&&!device.owned);
 frame(22);check(!m16_video_device_acquire(&device));
 check(m16_video_device_acquire(&device)<0&&f.held);
 check(m16_video_device_copy(&device,6143,a,2)<0);
 check(!m16_video_device_copy(&device,0,a,6144));check(!m16_video_device_release(&device));
 check(a[0]==11&&f.source[0]==0xcc&&!device.owned);check(m16_video_device_release(&device)<0);
 fresh();frame(23);f.file=fopen(argv[1],"wb");if(!f.file)return 3;
 for(t=0;t<10000&&!service.submitted_frames;t+=10)step(t,1);
 check(service.submitted_frames==1&&service.submitted_packets==7&&f.copies==1&&f.releases==1);
 check(!f.held&&service.slots[0].state==M16_VIDEO_FREE);check(fclose(f.file)==0);f.file=0;
 /* 音频优先时仍排空正在复制的槽，但不启动新复制或发送。 */
 fresh();frame(24);step(0,1);check(device.owned);step(1,0);check(!device.owned&&service.submitted_packets==0);
 step(2,0);step(3,0);check(f.sends==0&&service.acquired==1);step(4,1);check(f.sends==1);
 check(!m16_video_export_cancel(&service));
 /* 网络不接收时只有两个快照槽，过期丢帧，不覆盖活跃内存。 */
 fresh();frame(25);f.send_busy=1;
 for(t=0;t<=700000;t+=5000){if(t==200000)frame(26);if(t==400000)frame(27);step(t,1);}
 check(service.submitted_frames==0&&service.acquired>=2&&service.busy>0&&service.dropped>0);
 check(!m16_video_export_cancel(&service)&&!f.held);
 fresh();frame(28);step(0,1);check(f.held);step(500000,0);check(!f.held&&service.dropped==1&&f.copies==0);
 fresh();frame(29);f.copy_fail=1;step(0,1);step(1,1);check(!f.held&&service.dropped==1&&service.submitted_packets==0);
 fresh();frame(30);f.send_fail=1;for(t=0;t<100;t++)step(t,1);check(service.dropped==1&&service.submitted_frames==0);
 fresh();frame(31);f.release_fail=1;step(0,1);check(m16_video_export_poll(&service,1,1)<0);
 check(f.held&&device.owned&&device.fault&&service.fault);check(m16_video_export_cancel(&service)<0&&f.held);
 fresh();frame(32);step(10,1);check(m16_video_export_poll(&service,9,1)<0&&f.held);
 check(!m16_video_export_cancel(&service)&&!f.held);
 fresh();check(m16_video_export_init(&service,&device,a,a,6144,19,1,2,4,send,&f)<0);
 check(m16_video_export_init(&service,&device,a,b,6143,19,1,2,4,send,&f)<0);
 for(i=0;i<32;i++)check(a[6144+i]==0xa5&&b[6144+i]==0xa5);
 printf("VIDEO_EXPORT_C_PASS checks=%u errors=%u\n",checks,errors);return errors?1:0;
}
