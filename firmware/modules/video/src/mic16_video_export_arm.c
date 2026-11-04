/* ARM适配不分配全局图像载荷；快照池由主循环提供并与视频DDR隔离。 */
#include "mic16_video_export.h"
#include "mic16_video_pixels.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xpseudo_asm.h"
#include <string.h>
static uint32_t read_reg(void *context,unsigned offset){return Xil_In32((UINTPTR)context+offset);}
static int command(void *context,unsigned offset,uint32_t value){
 Xil_Out32((UINTPTR)context+offset,value);dsb();return 0;
}
static int copy_frame(void *context,uint32_t address,unsigned char *out,unsigned bytes){
 (void)context;
 /* 该区仅由PL写，CPU不作脏写。借用成功后失效缓存，再同步复制。
  * 内核调用前必须验证目标快照不与三幅视频槽重叠。 */
 if(!out||!bytes||(address&7u)||(bytes&7u))return -1;
 if(address<0x10000000u||address>=0x10240000u||bytes>0x10240000u-address)return -1;
 if((UINTPTR)out<0x10240000u&&(UINTPTR)out+bytes>0x10000000u)return -1;
 Xil_DCacheInvalidateRange((INTPTR)address,bytes);dsb();memcpy(out,(const void *)(UINTPTR)address,bytes);
 /* 仅在CPU快照中恢复每组四像素的次序；HDMI原通路不变。 */
 return m16_video_fifo64_to_rgb565(out,bytes);
}
int m16_video_export_arm_open(m16_video_device *device,uintptr_t control_base){
 m16_video_device_ops ops={(void *)control_base,read_reg,command,copy_frame};
 return m16_video_device_open(device,&ops);
}
