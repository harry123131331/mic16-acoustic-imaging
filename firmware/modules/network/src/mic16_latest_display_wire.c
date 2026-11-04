/* 最新窗口v3；原v2编码器独立保留，禁止把跳过分析当采样丢失。 */
#include "mic16_latest_display_wire.h"
#include <float.h>
#include <string.h>
static void p16(unsigned char *p,uint16_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void p32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4u;i++)p[i]=(unsigned char)(v>>(8u*i));}
static void p64(unsigned char *p,uint64_t v){for(unsigned i=0;i<8u;i++)p[i]=(unsigned char)(v>>(8u*i));}
static void pd(unsigned char *p,double x){uint64_t bits;memcpy(&bits,&x,8);p64(p,bits);}
static void pf(unsigned char *p,float x){uint32_t bits;memcpy(&bits,&x,4);p32(p,bits);}
static uint32_t crc(uint32_t v,const unsigned char *p,unsigned n){
 for(unsigned i=0;i<n;i++){v^=p[i];for(unsigned b=0;b<8u;b++)v=(v>>1)^((0u-(v&1u))&0xedb88320u);}return v;
}
int m16_latest_display_encode(const m16_fast_doa_result *r,uint32_t boot,uint64_t hz,
 unsigned part,unsigned char *out,unsigned capacity,unsigned *bytes){
 unsigned offset,count,n;uint32_t checksum;
 if(!r||!boot||!hz||!out||!bytes||!r->epoch||r->epoch>UINT32_MAX||r->end_frame<4u||
  r->first_frame!=r->end_frame-4u||r->end_frame>UINT64_MAX/2048u||
  r->first_sample!=r->first_frame*2048u||r->end_sample!=r->end_frame*2048u||
  r->grid>=325u||
  r->valid>1u||r->map_valid>1u||r->valid>r->map_valid||r->reason>2u||
  (r->valid!=(r->reason==0u))||r->last_front_tick<r->first_received_tick||
  r->completed_tick<r->last_front_tick||part>2u||(part&&!r->map_valid))return -1810;
 if(!(r->coherence>=-2.000005&&r->coherence<=2.000005)||
  !(r->contrast>=0.&&r->contrast<=4.00001)||!(r->support_fraction>=0.&&r->support_fraction<=1.)||
  !(r->azimuth_deg>=-60.&&r->azimuth_deg<=60.)||!(r->elevation_deg>=-30.&&r->elevation_deg<=30.))return -1810;
 if(r->map_valid&&(r->azimuth_deg!=-60.+5.*(double)(r->grid%25u)||
  r->elevation_deg!=-30.+5.*(double)(r->grid/25u)))return -1810;
 offset=part==2u?240u:0u;count=part==0u?5u:(part==1u?240u:85u);
 n=96u+count*(part?4u:8u);if(capacity<n)return -1811;
 if(part)for(unsigned i=0;i<count;i++)if(!(r->response[offset+i]>=-2.000005f&&r->response[offset+i]<=2.000005f))return -1810;
 memset(out,0,n);memcpy(out,part?"M16H":"M16D",4);p16(out+4,3u);p16(out+6,96u);
 p32(out+8,boot);p32(out+12,(uint32_t)r->epoch);p64(out+16,r->sequence);
 p64(out+24,r->first_sample);p64(out+32,r->end_sample);p64(out+40,r->completed_tick);p64(out+48,hz);
 p64(out+56,r->first_received_tick);p64(out+64,r->last_front_tick);
 p32(out+72,M16_FD_ALGORITHM);p32(out+76,r->valid|(r->map_valid<<1));p32(out+80,r->reason);p32(out+84,r->grid);
 p16(out+88,(uint16_t)offset);p16(out+90,(uint16_t)count);
 if(part)for(unsigned i=0;i<count;i++)pf(out+96u+i*4u,r->response[offset+i]);
 else{pd(out+96,r->azimuth_deg);pd(out+104,r->elevation_deg);pd(out+112,r->coherence);pd(out+120,r->contrast);pd(out+128,r->support_fraction);}
 checksum=~crc(crc(UINT32_MAX,out,92u),out+96u,n-96u);p32(out+92,checksum);*bytes=n;return 0;
}
