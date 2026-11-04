/* 使用真实视频快照状态机和受控相机寄存器，验证调度、归还和音频优先级。 */
#include "mic16_av_runtime.h"
#include "../firmware/modules/mic16_cooperate.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
extern void *fopen(const char *,const char *);
extern size_t fwrite(const void *,size_t,size_t,void *);
extern int fclose(void *);
static m16_av_runtime runtime;
static m16_video_export video;
static m16_video_device device;
static unsigned char snapshots[2][768000];
static uint64_t virtual_us;
static uint32_t commands,status,token,seq;
static unsigned owned,release_fail,copies,polls,sends,busy_send,fail_send,checks,errors,recurse;
static unsigned recorded;
static void *record;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("RUNTIME_CHECK_FAIL line=%d\n",__LINE__);}}while(0)
static uint64_t now(void *c){(void)c;return virtual_us;}
static uint32_t rd(void *c,unsigned o){
 (void)c;
 switch(o){case 0xa0:return 8u<<16;case 0xb8:return 0x4d565831u;case 0xbc:return status;
 case 0xc0:return token;case 0xc4:return seq;case 0xc8:return 0x10000000u;case 0xcc:return 768000u;
 case 0xd0:return (480u<<16)|800u;case 0xd4:return 1;case 0xd8:return commands;default:return 0;}
}
static int command(void *c,unsigned o,uint32_t v){
 (void)c;commands++;
 if(o==0xb0&&v==1&&!owned){owned=1;token++;seq++;status=3;return 0;}
 if(o==0xb4&&v==token&&owned){if(release_fail){status=0x20001;return 0;}owned=0;status=2;return 0;}
 status=0x20000;return 0;
}
static int copy(void *c,uint32_t address,unsigned char *out,unsigned n){
 unsigned i,offset=address-0x10000000u;(void)c;
 CHECK(owned&&n<=16384u&&offset+n<=768000u);
 for(i=0;i<n;i++)out[i]=(unsigned char)((offset+i)*37u+11u);
 copies++;return 0;
}
static void poll(void *c){
 (void)c;polls++;
 if(recurse){
  recurse=0;m16_av_runtime_step(&runtime);
  CHECK(m16_cooperate_bind(0,0)==-1);M16_COOPERATE();
 }
}
static uint32_t read32(const unsigned char *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int send(void *c,const unsigned char *p,unsigned n){
 uint32_t size=n;(void)c;
 CHECK(!owned&&!runtime.audio_depth&&!runtime.paused&&n<=1072);
 if(busy_send)return 1;
 if(fail_send)return -1;
 sends++;
 if(record&&read32(p+12)==1u){
  CHECK(fwrite(&virtual_us,8,1,record)==1&&fwrite(&size,4,1,record)==1&&fwrite(p,1,n,record)==n);recorded++;
 }
 return 0;
}
static void hook(void *c){(void)c;virtual_us+=100;m16_av_runtime_step(&runtime);}
static void setup(void){
 m16_video_device_ops d={0,rd,command,copy};m16_av_runtime_ops r={0,now,poll,send};
 CHECK(!owned);virtual_us=0;commands=status=token=seq=0;
 release_fail=copies=polls=sends=busy_send=fail_send=recurse=recorded=0;
 memset(&device,0,sizeof(device));memset(&video,0,sizeof(video));
 CHECK(!m16_video_device_open(&device,&d));
 CHECK(!m16_video_export_init(&video,&device,snapshots[0],snapshots[1],sizeof(snapshots[0]),20261020,1,1,1,send,0));
 CHECK(!m16_av_runtime_init(&runtime,&video,&r,8000000,4336,100));
 CHECK(!m16_cooperate_bind(hook,0));
}
static void advance(unsigned n){unsigned i;for(i=0;i<n;i++)M16_COOPERATE();}
#if defined(M16_RUNTIME_REPLAY) || defined(M16_RUNTIME_GOLDEN)
#if defined(M16_RUNTIME_REPLAY)
extern int m16_chain_main(int,char **);
#else
extern int m16_golden_main(void);
#endif
int main(int argc,char **argv){
 int rc;setup();record=fopen("runtime_video.bin","wb");CHECK(record!=0);
#if defined(M16_RUNTIME_REPLAY)
 rc=m16_chain_main(argc,argv);
#else
 (void)argc;(void)argv;rc=m16_golden_main();
#endif
 /* 即使零源/极短用例，也检查关闭前至少一份完整快照。虚拟时钟不代表ARM性能。 */
 advance(1400);CHECK(!runtime.fault&&video.submitted_frames>0&&recorded==768u);
 CHECK(copies>=47&&runtime.stats.steps>1000);
 CHECK(!m16_av_runtime_pause(&runtime)&&!owned);
 CHECK(!fclose(record));record=0;
 printf("AV_COOPERATIVE_REPLAY_%s checks=%u errors=%u checkpoints=%llu frames=%lu packets=%u VIRTUAL_TIME_NOT_BOARD\n",
  rc||errors?"FAIL":"PASS",checks,errors,runtime.stats.checkpoints,(unsigned long)video.submitted_frames,sends);
 return rc?rc:(errors?1:0);
}
#else
int main(void){
 unsigned old_sends,old_copies,old_polls;uint64_t credit;
 setup();CHECK(m16_av_runtime_audio_end(&runtime,100,0)<0);
 advance(1400);CHECK(!runtime.fault&&video.submitted_frames==1&&sends==768);
 CHECK(runtime.stats.video_bytes==768u*1072u&&runtime.stats.budget_denied>0);
 recurse=1;advance(1);CHECK(runtime.stats.reentries==1);
 CHECK(!m16_av_runtime_audio_begin(&runtime));CHECK(m16_av_runtime_audio_begin(&runtime)<0);
 old_sends=sends;old_copies=copies;old_polls=polls;advance(20);
 CHECK(sends==old_sends&&copies==old_copies&&polls>old_polls);
 credit=runtime.credits;CHECK(!m16_av_runtime_audio_end(&runtime,1084,0));
 CHECK(runtime.credits==(credit>1084?credit-1084:0)&&runtime.stats.audio_bytes==1084&&runtime.stats.audio_packets==1);
 CHECK(!m16_av_runtime_audio_begin(&runtime));credit=runtime.credits;
 CHECK(m16_av_runtime_audio_end(&runtime,1084,-1)==-1&&runtime.credits==credit);
 runtime.credits=0;CHECK(!m16_av_runtime_audio_begin(&runtime));
 CHECK(!m16_av_runtime_audio_end(&runtime,1084,0)&&runtime.audio_debt==1084);
 advance(1);CHECK(runtime.audio_debt==284&&runtime.credits==0);
 advance(1);CHECK(runtime.audio_debt==0&&runtime.credits==516);
 advance(600);CHECK(owned);CHECK(!m16_av_runtime_pause(&runtime));
 CHECK(!owned&&video.copy_slot==2&&video.slots[0].state==0&&video.slots[1].state==0);
 old_sends=sends;old_copies=copies;old_polls=polls;advance(100);
 CHECK(sends==old_sends&&copies==old_copies&&polls==old_polls);
 CHECK(m16_av_runtime_audio_begin(&runtime)<0);
 virtual_us+=5000000;CHECK(!m16_av_runtime_resume(&runtime)&&runtime.credits==0);
 advance(1400);CHECK(!runtime.fault&&sends>old_sends&&runtime.stats.capture_pauses==1);
 CHECK(runtime.stats.max_checkpoint_gap_us<=100&&runtime.stats.cancelled_frames==1);
 CHECK(!m16_av_runtime_pause(&runtime));setup();busy_send=1;advance(1400);
 CHECK(sends==0&&video.busy>0&&runtime.stats.video_bytes==0&&!owned);
 busy_send=0;fail_send=1;advance(1);CHECK(video.dropped==1&&sends==0);
 CHECK(!m16_av_runtime_pause(&runtime));setup();advance(2);CHECK(owned);
 release_fail=1;CHECK(m16_av_runtime_pause(&runtime)<0&&runtime.fault&&owned);
 old_copies=copies;advance(20);CHECK(copies==old_copies&&m16_av_runtime_resume(&runtime)<0);
 /* 模拟外部共同复位，只用于下一个独立用例，生产代码不得强制释放。 */
 owned=0;setup();advance(20);virtual_us--;
 m16_av_runtime_step(&runtime);CHECK(runtime.fault);owned=0;
 printf("AV_RUNTIME_%s checks=%u errors=%u scenarios=9 VIRTUAL_TIME_NOT_BOARD\n",errors?"FAIL":"PASS",checks,errors);
 return errors?1:0;
}
#endif
