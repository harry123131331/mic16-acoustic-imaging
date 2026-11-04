/* 完整800×480快照与768分片；归还时覆写原始槽，独立主机核验内容。 */
#include "mic16_video_export.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *fopen(const char *,const char *);
extern size_t fwrite(const void *,size_t,size_t,void *);
extern int fclose(void *);
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
enum { N=800*480*2 };
static unsigned char source[N],a[N+32],b[N+32];
static m16_video_device device;static m16_video_export service;
static unsigned checks,errors,held,complete,token,copied,copy_calls,releases;
static void *file;
static void check(int ok){checks++;if(!ok)errors++;}
static uint32_t read_reg(void *ctx,unsigned at){
 (void)ctx;
 switch(at){
  case 0xa0:return 8u<<16;case 0xb8:return 0x4d565831u;case 0xd4:return 1;
  case 0xd0:return 480u<<16|800u;case 0xcc:return N;case 0xd8:return complete;
  case 0xbc:return held?3:2;case 0xc0:return token;case 0xc4:return 23;case 0xc8:return 0x100c0000u;
  default:check(0);return 0;
 }
}
static int command(void *ctx,unsigned at,uint32_t value){
 (void)ctx;complete++;
 if(at==0xb0){check(!held&&value==1);held=1;token++;}
 else if(at==0xb4){check(held&&value==token&&copied==N);held=0;releases++;memset(source,0xcc,N);}
 else check(0);
 return 0;
}
static int copy(void *ctx,uint32_t address,unsigned char *out,unsigned n){
 (void)ctx;check(held&&device.owned&&n>0&&n<=M16_VIDEO_STEP_BYTES);
 check(address==0x100c0000u+copied&&n<=N-copied);memcpy(out,source+copied,n);copied+=n;copy_calls++;return 0;
}
static int send(void *ctx,const unsigned char *p,unsigned n){
 unsigned char size[4];(void)ctx;check(!held&&!device.owned&&copied==N&&releases==1);
 size[0]=(unsigned char)n;size[1]=(unsigned char)(n>>8);size[2]=0;size[3]=0;
 check(fwrite(size,1,4,file)==4&&fwrite(p,1,n,file)==n);return 0;
}
int main(int argc,char **argv){
 m16_video_device_ops ops={0,read_reg,command,copy};unsigned i,step,previous;
 if(argc!=2)return 2;
 for(i=0;i<N;i++)source[i]=(unsigned char)(i*37u+11u);
 memset(a,0xa5,sizeof(a));memset(b,0xa5,sizeof(b));
 check(!m16_video_device_open(&device,&ops));check(!m16_video_export_init(&service,&device,a,b,N,19,1,2,4,send,0));
 file=fopen(argv[1],"wb");if(!file)return 3;
 for(step=0;step<2000&&!service.submitted_frames;step++){
  previous=service.submitted_packets;check(m16_video_export_poll(&service,(uint64_t)step*10u,1)>=0);
  check(service.submitted_packets-previous<=1);
 }
 check(service.submitted_frames==1&&service.submitted_packets==768);
 check(copy_calls==47&&copied==N&&releases==1&&!held);
 for(i=N;i<N+32;i++)check(a[i]==0xa5&&b[i]==0xa5);
 check(!m16_video_export_cancel(&service));check(fclose(file)==0);
 printf("VIDEO_EXPORT_FULL_PASS checks=%u errors=%u copies=%u packets=%u\n",checks,errors,copy_calls,service.submitted_packets);
 return errors?1:0;
}
