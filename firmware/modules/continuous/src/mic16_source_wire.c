#include "mic16_source_wire.h"
static void u16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void u32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static void u64(unsigned char *p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(unsigned char)(v>>(i*8));}
static uint32_t crc(uint32_t c,const unsigned char *p,unsigned n){
    unsigned i,j;for(i=0;i<n;i++){c^=p[i];for(j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0u);}return c;
}
int m16_source_encode(const m16_cont_info *info,const double *a,const double *b,unsigned samples,
    m16_source_datagram_sink sink,void *context){
    unsigned char packet[M16_SOURCE_WIRE_MAX];unsigned offset=0,packets=0,i;
    uint64_t first,last;unsigned flags;
    if(!info || !sink || !info->epoch || info->stream_count>24 || info->current_confirmed>1 || info->final>1)return -1;
    first=((uint64_t)info->window*40960*256+624)/625;
    last=(((uint64_t)info->window+1)*40960*256+624)/625;
    if(info->output_start!=first || info->output_end!=last || samples!=last-first)return -1;
    if(!info->source_id){
        if(info->current_confirmed || info->ordinal!=info->stream_count)return -1;
        if(info->stream_count)return 0;
    }else if(!info->stream_count || info->ordinal>=info->stream_count || !a || !b)return -1;
    /* 发出任何包之前先验证整个声源。 */
    if(info->source_id)for(i=0;i<samples;i++){
        float x=(float)a[i],y=(float)b[i];if(!(x>=-3.402823466e38f && x<=3.402823466e38f && y>=-3.402823466e38f && y<=3.402823466e38f))return -1;
    }
    flags=info->current_confirmed|(info->final<<1)|(!info->source_id?4u:0u);
    do{
        unsigned count=info->source_id?(samples-offset<128?samples-offset:128):0;
        uint32_t checksum;
        packet[0]='M';packet[1]='1';packet[2]='6';packet[3]='S';u16(packet+4,1);u16(packet+6,flags);
        u32(packet+8,info->epoch);u32(packet+12,info->window);u32(packet+16,info->source_id);
        u16(packet+20,info->source_id?info->ordinal:0);u16(packet+22,info->stream_count);
        u64(packet+24,first);u64(packet+32,last);u32(packet+40,offset);u32(packet+44,samples);u32(packet+48,16000);
        u16(packet+52,count);u16(packet+54,0);
        for(i=0;i<count;i++){
            union{float f;uint32_t u;}x,y;x.f=(float)a[offset+i];y.f=(float)b[offset+i];
            u32(packet+60+i*8,x.u);u32(packet+64+i*8,y.u);
        }
        checksum=crc(0xffffffffu,packet,56);checksum=crc(checksum,packet+60,count*8)^0xffffffffu;u32(packet+56,checksum);
        if(sink(context,packet,60+count*8))return -2;
        packets++;offset+=count;
        if(!info->source_id)break;
    }while(offset<samples);
    return (int)packets;
}
