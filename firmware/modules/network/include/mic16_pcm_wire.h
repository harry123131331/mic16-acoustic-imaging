#ifndef MIC16_PCM_WIRE_H
#define MIC16_PCM_WIRE_H
#include <stdint.h>
/* 未增强、未降采样的16路F32 PCM，与定位器输入逐位相同。
 * 2048样本子帧分128包，每包16样本×16通道；CRC和样本轴独立于接收时钟。 */
typedef struct {
 unsigned magic,epoch,frame,cursor,pending,bytes,scale_exponent;
 uint64_t first_sample;
 const float *pcm;
 unsigned char packet[1088];
} m16_pcm_wire;
int m16_pcm_wire_init(m16_pcm_wire *);
int m16_pcm_wire_begin(m16_pcm_wire *,const float *,unsigned epoch,uint64_t first_sample,unsigned scale_exponent);
/* 1有稳定报文，0整帧完成，负数无效。ack前不移动PCM借用/游标。 */
int m16_pcm_wire_packet(m16_pcm_wire *,const unsigned char **,unsigned *);
int m16_pcm_wire_ack(m16_pcm_wire *);
void m16_pcm_wire_stop(m16_pcm_wire *);
#endif
