#ifndef MIC16_VIDEO_PIXELS_H
#define MIC16_VIDEO_PIXELS_H
/* 相机IP1.2的16→64 FIFO将先到像素放在高16位。
 * 在CPU自有快照中恢复成逐像素RGB565小端序；不修改PL拥有的DDR槽。
 * bytes必须是非零8字节倍数；分块起始也必须由调用方保证按8字节对齐。 */
int m16_video_fifo64_to_rgb565(unsigned char *pixels,unsigned bytes);
#endif
