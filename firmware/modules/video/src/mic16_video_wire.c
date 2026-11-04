#include "mic16_video_wire.h"
#include <stddef.h>
/* 标准CRC-32/ISO-HDLC；不共享可变全局状态。 */
static uint32_t update(uint32_t crc,const unsigned char *p,unsigned n){
 unsigned i,j;for(i=0;i<n;i++){crc^=p[i];for(j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}return crc;
}
static int overlap(const void *,unsigned,const void *,unsigned);
int m16_video_crc32(const unsigned char *p,unsigned n,uint32_t *out){
 if(!p || !out || !n || n>M16V_MAX_BYTES || overlap(p,n,out,sizeof(*out)))return -1;
 *out=update(0xffffffffu,p,n)^0xffffffffu;return 0;
}
static int overlap(const void *a,unsigned na,const void *b,unsigned nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;
}
static void w16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void w32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
int m16_video_packet(const m16_video_meta *m,const unsigned char *frame,unsigned bytes,unsigned index,
 unsigned char *out,unsigned capacity,unsigned *written){
 unsigned bpp,chunks,offset,n,i;uint32_t crc;
 if(!m || !frame || !out || !written || !m->epoch || !m->mount_id || !m->mode_id || !m->transform_id ||
  !m->width || m->width>800 || !m->height || m->height>480 || m->flags>1 ||
  ((m->flags&M16V_TIME_VERIFIED)&&!m->clock_domain) ||
  (m->format!=M16V_RGB565_LE && m->format!=M16V_RGB888))return -1;
 bpp=m->format==M16V_RGB565_LE?2u:3u;
 if(bytes!=(unsigned)m->width*m->height*bpp || bytes>M16V_MAX_BYTES)return -1;
 chunks=(bytes+M16V_PAYLOAD-1)/M16V_PAYLOAD;if(index>=chunks)return -1;
 offset=index*M16V_PAYLOAD;n=bytes-offset;if(n>M16V_PAYLOAD)n=M16V_PAYLOAD;
 if(capacity<M16V_HEADER+n || overlap(out,M16V_HEADER+n,frame,bytes) ||
  overlap(out,M16V_HEADER+n,m,sizeof(*m)) || overlap(out,M16V_HEADER+n,written,sizeof(*written)) ||
  overlap(written,sizeof(*written),frame,bytes) || overlap(written,sizeof(*written),m,sizeof(*m)))return -1;
 out[0]='M';out[1]='1';out[2]='6';out[3]='V';w16(out+4,1);w16(out+6,M16V_HEADER);
 w32(out+8,m->epoch);w32(out+12,m->frame_id);w32(out+16,(uint32_t)m->capture_us);w32(out+20,(uint32_t)(m->capture_us>>32));
 w16(out+24,m->width);w16(out+26,m->height);w16(out+28,m->format);w16(out+30,m->flags);
 w32(out+32,m->mount_id);w32(out+36,m->mode_id);w32(out+40,m->clock_domain);
 w32(out+44,bytes);w32(out+48,offset);w32(out+52,m->frame_crc32);
 w16(out+56,index);w16(out+58,chunks);w16(out+60,n);w16(out+62,0);w32(out+64,m->transform_id);
 for(i=0;i<n;i++)out[M16V_HEADER+i]=frame[offset+i];
 crc=update(0xffffffffu,out,68);crc=update(crc,out+M16V_HEADER,n)^0xffffffffu;w32(out+68,crc);
 *written=M16V_HEADER+n;return 0;
}
