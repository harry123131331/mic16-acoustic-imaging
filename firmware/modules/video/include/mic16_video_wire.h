#ifndef MIC16_VIDEO_WIRE_H
#define MIC16_VIDEO_WIRE_H
#include <stdint.h>
enum { M16V_HEADER=72, M16V_PAYLOAD=1000, M16V_MAX_PACKET=1072,
 M16V_MAX_BYTES=1152000, M16V_RGB565_LE=1, M16V_RGB888=2, M16V_TIME_VERIFIED=1 };
/* 时间来自采集时刻；未核验时flags=0。此编码器不获取或占用硬件帧。 */
typedef struct {
 uint32_t epoch,frame_id,mount_id,mode_id,clock_domain,transform_id,frame_crc32;
 uint64_t capture_us;
 uint16_t width,height,format,flags;
} m16_video_meta;
int m16_video_crc32(const unsigned char *,unsigned,uint32_t *);
/* 调用方须在整帧独占且不再写入时算一次frame_crc32；编码期间保持只读。
 * 输出与输入、meta、written均分离；失败不改输出和written。CRC用于检错，不用于鉴权。 */
int m16_video_packet(const m16_video_meta *,const unsigned char *,unsigned,unsigned,
 unsigned char *,unsigned,unsigned *);
#endif
