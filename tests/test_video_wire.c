/* 独立C编码夹具；Python另行解包/组帧，避免用同一编码器证明自己。 */
#include "mic16_video_wire.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *fopen(const char *,const char *);
extern size_t fwrite(const void *,size_t,size_t,void *);
extern int fclose(void *);
extern void *memset(void *,int,size_t);
static unsigned checks,errors;
static unsigned char frame[64*48*2],packet[M16V_MAX_PACKET];
static void check(int ok){checks++;if(!ok)errors++;}
int main(int argc,char **argv){
 m16_video_meta meta,invalid;unsigned i,n=12345,chunks;uint32_t crc=0;void *file;unsigned char size[4];
 if(argc!=2)return 2;
 memset(&meta,0,sizeof(meta));
 check(m16_video_crc32((const unsigned char *)"123456789",9,&crc)==0 && crc==0xcbf43926u);
 check(m16_video_crc32(0,9,&crc)==-1);check(m16_video_crc32(frame,0,&crc)==-1);
 check(m16_video_crc32(frame,M16V_MAX_BYTES+1,&crc)==-1);
 check(m16_video_crc32(frame,sizeof(frame),0)==-1);
 check(m16_video_crc32(frame,sizeof(frame),(uint32_t *)frame)==-1);
 for(i=0;i<sizeof(frame);i++)frame[i]=(unsigned char)(i*37u+11u);
 check(!m16_video_crc32(frame,sizeof(frame),&crc));
 meta.epoch=19;meta.frame_id=23;meta.capture_us=1000000;meta.width=64;meta.height=48;
 meta.format=M16V_RGB565_LE;meta.flags=M16V_TIME_VERIFIED;meta.mount_id=1;meta.mode_id=2;
 meta.clock_domain=3;meta.transform_id=4;meta.frame_crc32=crc;
 invalid=meta;invalid.width=801;
 memset(packet,0xa5,sizeof(packet));
 check(m16_video_packet(&invalid,frame,sizeof(frame),0,packet,sizeof(packet),&n)==-1);
 check(packet[0]==0xa5 && n==12345);
 check(m16_video_packet(&meta,frame,sizeof(frame),0,packet,72,&n)==-1);
 check(packet[0]==0xa5 && n==12345);
 check(m16_video_packet(&meta,frame,sizeof(frame),99,packet,sizeof(packet),&n)==-1);
 check(m16_video_packet(&meta,frame,sizeof(frame)-1,0,packet,sizeof(packet),&n)==-1);
 check(m16_video_packet(&meta,frame,sizeof(frame),0,frame,sizeof(frame),&n)==-1);
 invalid=meta;invalid.clock_domain=0;
 check(m16_video_packet(&invalid,frame,sizeof(frame),0,packet,sizeof(packet),&n)==-1);
 invalid=meta;invalid.flags=0;
 check(!m16_video_packet(&invalid,frame,sizeof(frame),0,packet,sizeof(packet),&n));
 file=fopen(argv[1],"wb");if(!file)return 3;
 chunks=(sizeof(frame)+M16V_PAYLOAD-1)/M16V_PAYLOAD;
 for(i=0;i<chunks;i++){
  check(!m16_video_packet(&meta,frame,sizeof(frame),i,packet,sizeof(packet),&n));
  size[0]=(unsigned char)n;size[1]=(unsigned char)(n>>8);size[2]=(unsigned char)(n>>16);size[3]=(unsigned char)(n>>24);
  check(fwrite(size,1,4,file)==4);check(fwrite(packet,1,n,file)==n);
 }
 check(fclose(file)==0);
 printf("VIDEO_WIRE_C_%s checks=%u packets=%u errors=%u\n",errors?"FAIL":"PASS",checks,chunks,errors);
 return errors?1:0;
}
