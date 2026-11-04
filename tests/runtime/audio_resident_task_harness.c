/* 测试工厂复用实际检测/增强/重建任务及原冻结参考，不替代生产总控。 */
#include "audio_pipeline_task_harness.c"
#include "mic16_audio_resident_task.h"
#include "mic16_heatmap_wire.h"
#include <stdlib.h>
extern int wc_reset(unsigned,unsigned,unsigned,unsigned),wc_complete(unsigned,unsigned,unsigned,unsigned);
extern void wc_reset_done(void);
extern uint64_t wc_metric(unsigned);
extern m16_window_capture *ar_capture_bridge(void);
static mic16_stream_scheduler resident_scheduler;
static m16_capture_stream resident_capture;
static m16_output_pool resident_output;
static m16_resident_control resident_control;
static m16_audio_resident_task resident;
static float resident_storage[M16_OUTPUT_POOL_FLOATS];
static m16_heatmap_workspace resident_reference_map;
typedef struct expected_packet {struct expected_packet *next;unsigned bytes;unsigned char data[1084];} expected_packet;
static expected_packet *heads[64][3],*tails[64][3];
static uint64_t checked_packets,checked_bytes,queued_packets,checked_pcm,checked_locations;
static unsigned command_id,block_send,fail_send,run_reference,finished_reference;
static int reference_error;
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int packet_key(const unsigned char *p,unsigned bytes,unsigned *window,unsigned *kind){
 if(bytes<20||memcmp(p,"M16",3))return -1200;
 *window=u32(p+12);*kind=p[3]=='L'?0u:p[3]=='H'?1u:p[3]=='S'?2u:3u;
 return *window<64u&&*kind<3u?0:-1201;
}
static int save_packet(void *unused,const unsigned char *data,unsigned bytes){
 expected_packet *p;unsigned window,kind;int rc=packet_key(data,bytes,&window,&kind);(void)unused;
 if(rc||bytes>1084u)return -1202;
 p=(expected_packet *)malloc(sizeof(*p));if(!p)return -1203;
 p->next=0;p->bytes=bytes;memcpy(p->data,data,bytes);
 if(tails[window][kind])tails[window][kind]->next=p;else heads[window][kind]=p;
 tails[window][kind]=p;queued_packets++;return 0;
}
static int send_packet(void *unused,unsigned port,const unsigned char *data,unsigned bytes){
 unsigned window,kind;expected_packet *p;int rc;(void)unused;
 if(fail_send)return -1204;
 if(block_send)return 1;
 if(!run_reference)return 0;
 rc=packet_key(data,bytes,&window,&kind);if(rc)return rc;
 p=heads[window][kind];if(!p||p->bytes!=bytes||memcmp(p->data,data,bytes)||port!=(kind==1u?5003u:5001u))return -1205;
 heads[window][kind]=p->next;if(!p->next)tails[window][kind]=0;
 free(p);queued_packets--;checked_packets++;checked_bytes+=bytes;return 0;
}
static void clear_packets(void){unsigned i,j;for(i=0;i<64;i++)for(j=0;j<3;j++)while(heads[i][j]){
 expected_packet *p=heads[i][j];heads[i][j]=p->next;free(p);tails[i][j]=0;
}}
int ar_setup(const float *x,const float *q,unsigned overlap,unsigned warmup){
 m16_resident_control control_copy;m16_audio_resident_io io={0,send_packet};mic16_stream_config config={3,3,1000000,20000000,40000000,1,1};
 m16_window_capture *c;int rc;
 clear_packets();checked_packets=checked_bytes=queued_packets=checked_pcm=checked_locations=0;
 command_id=block_send=fail_send=finished_reference=0;run_reference=1;reference_error=0;
 rc=wc_reset(24,3,warmup,overlap?8u:0u);if(rc)return rc;
 rc=ap_setup(x,q);if(rc)return rc;
 c=ar_capture_bridge();
 memset(&resident_scheduler,0,sizeof(resident_scheduler));memset(&resident_capture,0,sizeof(resident_capture));
 memset(&resident_output,0,sizeof(resident_output));memset(&resident_control,0,sizeof(resident_control));memset(&resident,0,sizeof(resident));
 rc=mic16_stream_init(&resident_scheduler,&config,1,wc_metric(18));if(rc)return rc;
 rc=m16_capture_stream_init(&resident_capture,c,&resident_scheduler);if(rc)return rc;
 rc=m16_output_pool_init(&resident_output,&resident_scheduler,resident_storage,M16_OUTPUT_POOL_FLOATS);if(rc)return rc;
 rc=m16_control_init(&resident_control,0x1234u,13u);if(rc)return rc;
 control_copy=resident_control;
 rc=m16_audio_resident_init(&resident,&resident_capture,&audio_pipeline,&resident_output,&resident_control,q,"pose:1/ref:1/source:1",&io);
 if(overlap&&rc!=M16_AR_INVALID)return -1206;
 if(overlap&&memcmp(&resident_control,&control_copy,sizeof(control_copy)))return -1207;
 return rc;
}
static uint32_t command_crc(const unsigned char *p,unsigned n){uint32_t v=~0u;for(unsigned i=0;i<n;i++){v^=p[i];for(unsigned j=0;j<8;j++)v=(v>>1)^(0xedb88320u&(0u-(v&1u)));}return ~v;}
static void word(unsigned char *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(i*8));}
int ar_command(unsigned op){
 unsigned char p[32]={0},reply[80];int rc;
 memcpy(p,"M16C",4);p[4]=1;p[6]=(unsigned char)op;word(p+8,++command_id);word(p+12,op==M16_CTL_HELLO?0:0x1234u);p[16]=7;
 word(p+28,command_crc(p,28));rc=m16_control_handle(&resident_control,p,32,reply,80,wc_metric(18)/1000u);
 return rc==80?(int32_t)u32(reply+36):rc;
}
static int reference_ready(void){
 int rc;
 if(resident.computing&&audio_pipeline.phase==M16_AP_DETECT&&!compare_enabled){
  pcm=resident.input.pcm;quiet=resident.reference;calls=ready=checks=0;base=served;expected_built=expected_count=consumed_audio=0;
  rc=ap_reference_detection();if(rc)return rc;
  /* 比较底层寄存器/缓存模型交付的PCM与旧逐包归一化算法。 */
  {float *copy=(float *)malloc(M16_WC_BYTES);if(!copy)return -1208;
   extern int wc_reference(unsigned,float *);rc=wc_reference((unsigned)(resident.computed_windows+1u),copy);
   if(!rc&&memcmp(copy,pcm,M16_WC_BYTES))rc=-1209;
   free(copy);if(rc)return rc;checked_pcm+=M16_WC_VALUES;}
 }
 if(resident.computing&&audio_pipeline.phase==M16_AP_SOURCE_AUDIO&&!audio_pipeline.finishing&&!expected_built){
  expected_built=1;rc=build_reference();if(rc)return rc;
  rc=m16_directions_encode(source_audio.epoch,(uint32_t)resident.computed_windows,&original_tracks,save_packet,0);if(rc!=1)return -1210;
  rc=m16_heatmap_encode(source_audio.epoch,(uint32_t)resident.computed_windows,expected_maps,&resident_reference_map,save_packet,0);if(rc!=2)return -1211;
  checked_locations++;
  for(unsigned i=0;i<expected_count;i++){rc=m16_source_encode(expected_info+i,expected_audio[i][0],expected_audio[i][1],expected_size[i],save_packet,0);if(rc<0)return rc;}
 }
 if(resident.computing&&audio_pipeline.finishing&&!finished_reference){
  finished_reference=1;expected_count=consumed_audio=0;
  rc=m16_cont_finish(&original_continuous,audio_sink,0);if(rc<0)return rc;
  for(unsigned i=0;i<expected_count;i++){rc=m16_source_encode(expected_info+i,expected_audio[i][0],expected_audio[i][1],expected_size[i],save_packet,0);if(rc<0)return rc;}
 }
 return 0;
}
int ar_steps(unsigned limit){
 int rc=1;uint64_t before;
 for(unsigned i=0;i<limit;i++){
  if(run_reference){rc=reference_ready();if(rc){reference_error=rc;return rc;}}
  rc=dt_service(1);if(rc<0)return rc;
  before=resident.computed_windows;rc=m16_audio_resident_step(&resident);
  if(before!=resident.computed_windows){
   if(run_reference&&!ap_equal())return -1212;
   compare_enabled=expected_built=0;
  }
  if(rc<0||resident.phase==M16_AR_STOPPED||resident.phase==M16_AR_FAILED)return rc;
 }
 return rc;
}
int ar_complete(unsigned ordinal,unsigned flags){return wc_complete(ordinal,M16_WC_BYTES-(flags==1u?4u:0u),0x1002u,0);}
void ar_send_mode(unsigned block,unsigned fault){block_send=block;fail_send=fault;}
void ar_compare(unsigned enabled){run_reference=enabled;}
void ar_dense(unsigned enabled){override_scores=enabled;}
int ar_restart_reference(void){
 clear_packets();queued_packets=0;compare_enabled=expected_built=finished_reference=0;
 mic16_memory_init(&original_memory);memset(&original_parameters,0,sizeof(original_parameters));
 return m16_cont_init(&original_continuous,resident_control.epoch);
}
int64_t ar_metric(unsigned key){
 switch(key){case 0:return resident.phase;case 1:return resident.error;case 2:return resident.computed_windows;
 case 3:return resident_control.state;case 4:return resident_control.epoch;case 5:return resident.input_held;
 case 6:return resident.computing;case 7:return checked_packets;case 8:return checked_bytes;case 9:return queued_packets;
 case 10:return resident.backpressure;case 11:return m16_audio_resident_releasable(&resident);
 case 12:return checked_pcm;case 13:return compared_detection_values;case 14:return checked_locations;
 case 15:return resident_output.windows_sent;case 16:return audio_pipeline.phase;case 17:return cs_metric(0);
 case 18:return resident_scheduler.stats.input_overruns;case 19:return resident_scheduler.stats.outputs_sent;
 case 20:return resident.steps;case 21:return reference_error;case 22:return resident_control.compute_us;
 case 23:return m16_control_take_action(&resident_control);default:return -1;}
}
