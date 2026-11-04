#include "mic16_directions_wire.h"
static void u16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void u32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static void u64(unsigned char *p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(unsigned char)(v>>(i*8));}
static void fp64(unsigned char *p,double v){union{double f;uint64_t u;}x;x.f=v;u64(p,x.u);}
static uint32_t crc(uint32_t c,const unsigned char *p,unsigned n){unsigned i,j;for(i=0;i<n;i++){c^=p[i];for(j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0u);}return c;}
int m16_directions_encode(uint32_t epoch,uint32_t window,const mic16_cycle_output *event,m16_source_datagram_sink sink,void *context){
    unsigned char bytes[260];unsigned i,j,count=0;uint32_t sum;
    if(!epoch || !event || !sink || event->count<0 || event->count>8 || event->confirmed_count<0 || event->confirmed_count>event->count)return -1;
    for(i=0;i<(unsigned)event->count;i++){
        const mic16_track *t=event->tracks+i;
        if(!t->source_id || (t->confirmed_this_cycle!=0 && t->confirmed_this_cycle!=1))return -1;
        for(j=0;j<i;j++)if(event->tracks[j].source_id==t->source_id)return -1;
        if(!t->confirmed_this_cycle)continue;
        if(!(t->azimuth>-90. && t->azimuth<90. && t->elevation>-90. && t->elevation<90. && t->score>=.65 && t->score<=1.))return -1;
        u32(bytes+36+count*28,t->source_id);fp64(bytes+40+count*28,t->azimuth);
        fp64(bytes+48+count*28,t->elevation);fp64(bytes+56+count*28,t->score);count++;
    }
    if(count!=(unsigned)event->confirmed_count)return -1;
    bytes[0]='M';bytes[1]='1';bytes[2]='6';bytes[3]='L';u16(bytes+4,1);u16(bytes+6,count);
    u32(bytes+8,epoch);u32(bytes+12,window);u64(bytes+16,(uint64_t)window*40960);u64(bytes+24,((uint64_t)window+1)*40960);
    sum=crc(0xffffffffu,bytes,32);sum=crc(sum,bytes+36,count*28)^0xffffffffu;u32(bytes+32,sum);
    return sink(context,bytes,36+count*28)?-2:1;
}
