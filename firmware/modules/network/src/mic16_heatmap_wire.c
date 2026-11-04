#include "mic16_heatmap_wire.h"
#include "mic16_cooperate.h"
static void u16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void u32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static void u64(unsigned char *p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(unsigned char)(v>>(i*8));}
static void f32(unsigned char *p,float v){union{float f;uint32_t u;}x;x.f=v;u32(p,x.u);}
static uint32_t crc(uint32_t c,const unsigned char *p,unsigned n){unsigned i,j;for(i=0;i<n;i++){c^=p[i];for(j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0u);}return c;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
 __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;return x<=y?y-x<na:x-y<nb;}
int m16_heatmap_encode(uint32_t epoch,uint32_t window,const float *maps,m16_heatmap_workspace *w,m16_source_datagram_sink sink,void *context){
 unsigned g,t,offset,count;unsigned char *p;uint32_t sum;
 if(!epoch||!maps||!w||!sink||overlap(maps,480ul*325*4,w,sizeof(*w)))return -1;
 /* 先校验全部输入，非法空间谱不会发布半窗或伪零图。 */
 for(g=0;g<480*325;g++){if((g&4095u)==0)M16_COOPERATE();if(!(maps[g]>=-2.f&&maps[g]<=2.f))return -1;}
 for(g=0;g<325;g++)w->sums[g]=0.;
 for(t=0;t<480;t++){if((t&31u)==0)M16_COOPERATE();for(g=0;g<325;g++)w->sums[g]+=(double)maps[t*325+g];}
 for(g=0;g<325;g++)w->values[g]=(float)(w->sums[g]/480.);
 p=w->packet;p[0]='M';p[1]='1';p[2]='6';p[3]='H';u16(p+4,1);u16(p+6,72);
 u32(p+8,epoch);u32(p+12,window);u64(p+16,(uint64_t)window*40960);u64(p+24,((uint64_t)window+1)*40960);
 u16(p+32,13);u16(p+34,25);u16(p+36,1);u16(p+38,1);
 f32(p+40,-60.f);f32(p+44,-30.f);f32(p+48,5.f);f32(p+52,5.f);u32(p+56,1);
 for(offset=0;offset<325;offset+=240){
  count=325-offset;if(count>240)count=240;u32(p+60,offset);u32(p+64,count);
  for(g=0;g<count;g++)f32(p+72+g*4,w->values[offset+g]);
  sum=crc(0xffffffffu,p,68);sum=crc(sum,p+72,count*4)^0xffffffffu;u32(p+68,sum);
  if(sink(context,p,72+count*4))return -2;
 }
 return 2;
}
