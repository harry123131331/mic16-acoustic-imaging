#include "mic16_av_policy.h"
#ifdef M16_AV_LLVM_HOST_NO_SDK
#include <stddef.h>
extern void *memset(void *,int,size_t);
#else
#include <string.h>
#endif
int m16_av_audio_dma_destination(uint32_t address,uint32_t bytes) {
    return address>=0x00100000u && address%64u==0 && bytes!=0 &&
        (uint64_t)address+bytes<=M16_VIDEO_DDR_BEGIN;
}
int m16_av_publish_checked(const m16_av_calibration *cal,const mic16_cycle_output *cycle,
    uint32_t mount,uint32_t mode,uint32_t verified,uint64_t end,uint64_t now,uint32_t age,
    void *ctx,m16_av_read32 rd,m16_av_write32 wr,int *reason,uint32_t *omitted) {
    m16_av_frame frame;int rc;
    memset(&frame,0,sizeof(frame));
    if(!reason || !omitted)return -1;
    *omitted=0;
    rc=verified==1?m16_av_build(cal,cycle,mount,mode,end,now,age,&frame,omitted):M16_AV_INVALID;
    *reason=rc;
    /* 异常声源被拒绝时，底层构建函数可能已写入部分字段；不得将其作为有效帧提交。 */
    if(rc)memset(&frame,0,sizeof(frame));
    return m16_av_submit(&frame,ctx,rd,wr);
}
