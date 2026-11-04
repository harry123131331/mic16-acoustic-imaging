#ifndef MIC16_VIDEO_EXPORT_H
#define MIC16_VIDEO_EXPORT_H
#include "mic16_video_wire.h"
/* command返回前必须完成AXI写响应；copy为同步复制，返回后不保留指针。 */
typedef struct {
 void *context;
 uint32_t (*read)(void *,unsigned);
 int (*command)(void *,unsigned,uint32_t);
 int (*copy)(void *,uint32_t,unsigned char *,unsigned);
} m16_video_device_ops;
typedef struct {
 m16_video_device_ops ops;
 uint32_t address,token,sequence,bytes;
 unsigned width,height,opened,owned,fault;
} m16_video_device;
int m16_video_device_open(m16_video_device *,const m16_video_device_ops *);
/* 0成功、1暂无新完整帧、负数故障。故障或未归还时不允许复用。 */
int m16_video_device_acquire(m16_video_device *);
int m16_video_device_release(m16_video_device *);
int m16_video_device_copy(m16_video_device *,unsigned,unsigned char *,unsigned);
int m16_video_export_arm_open(m16_video_device *,uintptr_t);

enum { M16_VIDEO_FREE=0,M16_VIDEO_COPY=1,M16_VIDEO_CRC=2,M16_VIDEO_SEND=3,M16_VIDEO_STEP_BYTES=16384 };
typedef struct {
 unsigned char *pixels;
 unsigned state,offset,index;
 uint32_t crc;
 uint64_t started_us;
 m16_video_meta meta;
} m16_video_slot;
/* 发送回调不保留packet指针；0仅表示提交，1稍后重试，负数终止本帧。 */
typedef int (*m16_video_try_send)(void *,const unsigned char *,unsigned);
typedef struct {
 m16_video_device *device;
 m16_video_slot slots[2];
 m16_video_try_send send;
 void *send_context;
 unsigned char packet[M16V_MAX_PACKET];
 uint32_t epoch,mount_id,mode_id,transform_id;
 uint32_t last_sequence;
 uint64_t last_now,next_capture_us;
 unsigned copy_slot,have_sequence,clock_started,fault;
 uint32_t acquired,submitted_frames,submitted_packets,dropped,busy,no_frame;
} m16_video_export;
int m16_video_export_init(m16_video_export *,m16_video_device *,unsigned char *,unsigned char *,unsigned,
 uint32_t,uint32_t,uint32_t,uint32_t,m16_video_try_send,void *);
/* 每次最多复制/校验16KiB或提交一包；allow_video=0仍优先排空已有复制并归还槽。
 * 需由统一主循环频繁调用，不能在8秒计算之间才调用一次；不是实时吞吐保证。 */
int m16_video_export_poll(m16_video_export *,uint64_t,int);
/* 同步回调已停止访问后才能取消；归还失败保持故障，不强行重用内存。 */
int m16_video_export_cancel(m16_video_export *);
#endif
