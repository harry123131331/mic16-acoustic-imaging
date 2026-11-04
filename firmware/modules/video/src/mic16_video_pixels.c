#include "mic16_video_pixels.h"
int m16_video_fifo64_to_rgb565(unsigned char *pixels,unsigned bytes){
 unsigned i;
 if(!pixels||!bytes||(bytes&7u))return -1;
 for(i=0;i<bytes;i+=8){
  unsigned char a=pixels[i],b=pixels[i+1],c=pixels[i+2],d=pixels[i+3];
  /* 只反转每组四个16位像素的次序，像素内部两个字节的顺序保持。 */
  pixels[i]=pixels[i+6];pixels[i+1]=pixels[i+7];
  pixels[i+2]=pixels[i+4];pixels[i+3]=pixels[i+5];
  pixels[i+4]=c;pixels[i+5]=d;pixels[i+6]=a;pixels[i+7]=b;
 }
 return 0;
}
