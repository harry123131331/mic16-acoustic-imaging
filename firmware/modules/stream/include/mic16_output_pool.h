#ifndef MIC16_OUTPUT_POOL_H
#define MIC16_OUTPUT_POOL_H
#include "mic16_stream_scheduler.h"
#include "mic16_output_wire_task.h"
#include "mic16_directions_wire.h"

#define M16_OUTPUT_SOURCE_FLOATS (M16_CONT_OUTPUT*2u)
#define M16_OUTPUT_SLOT_FLOATS (M16_CONT_MAX_AUDIO_IDS*M16_OUTPUT_SOURCE_FLOATS)
#define M16_OUTPUT_POOL_FLOATS (3u*M16_OUTPUT_SLOT_FLOATS)
enum {M16_OP_COPY_IDLE=0,M16_OP_COPY_ACTIVE=1,M16_OP_COPY_READY=2};
enum {M16_OP_DIRECTION=1,M16_OP_HEATMAP=2,M16_OP_AUDIO=3};
enum {M16_OP_INVALID=-1000,M16_OP_BUSY=-1001,M16_OP_INPUT=-1002,M16_OP_LEASE=-1003,M16_OP_COUNTER=-1004};
typedef struct {
 uintptr_t owner;
 uint64_t serial,generation;
 uint32_t epoch,window,slot,kind;
} m16_output_packet_ticket;
typedef struct {
 const unsigned char *data;
 unsigned bytes,port;
 m16_output_packet_ticket ticket;
} m16_output_packet_view;
typedef struct {
 unsigned bound,location_ready,location_sent,location_bytes;
 unsigned count_set,stream_count,audio_count,reference_seen,audio_verified,samples;
 uint32_t epoch,window;
 mic16_stream_ticket ticket;
 m16_cont_info audio[24],reference;
 m16_heatmap_wire_task heatmap;
 unsigned char location[260];
} m16_output_pool_slot;
/* 只负责CPU输出存储，不接管DMA/PL。调度器是槽所有权的唯一来源。
 * 整个对象及storage放DDR/BSS；storage最少output_slots*24*16778*2个float。
 * 3槽完整上限为9,664,128字节。结构内没有第二份PCM或整窗空间谱。 */
typedef struct {
 unsigned magic,epoch,stopping,map_cursor;
 unsigned copy_phase,copy_slot,copy_index,copy_samples;
 unsigned send_active,send_slot,send_index,audio_started,pending;
 int error;
 uint64_t packet_serial,packets_sent,windows_sent,copied_pairs;
 uintptr_t storage_bytes;
 float *storage;
 mic16_stream_scheduler *scheduler;
 m16_output_pool_slot slots[3];
 m16_cont_info copy_info;
 const double *copy_a,*copy_b;
 m16_audio_wire_task audio_wire;
 m16_wire_ticket wire_ticket;
 m16_output_packet_view packet;
 unsigned localization_only;
} m16_output_pool;

#ifdef __cplusplus
extern "C" {
#endif
/* 所有调用由同一主循环所有者串行执行，不得在ISR/网络回调运行。 */
int m16_output_pool_init(m16_output_pool *,mic16_stream_scheduler *,float *storage,unsigned storage_floats);
/* 输入仍由COMPUTE持有时，先由scheduler预留BUILD再bind；本层不创建第二套槽状态。 */
int m16_output_pool_set_localization_only(m16_output_pool *,unsigned);
/* 仅定位结果完成后提交，不伪造MIC0或空增强音频。 */
int m16_output_pool_location_commit(m16_output_pool *,uint32_t,uint32_t,uint64_t);
int m16_output_pool_bind(m16_output_pool *,const mic16_stream_ticket *,uint64_t now);
/* 定位先于音频发布；maps一直借用至location_releasable，根任务才可释放定位租约。 */
int m16_output_pool_location(m16_output_pool *,const mic16_stream_ticket *,const mic16_cycle_output *,const float *maps,uint64_t now);
int m16_output_pool_location_releasable(const m16_output_pool *,const mic16_stream_ticket *);
/* 借用连续重建输出，分步转为输出槽F32；复制完成后须同时归还根任务和本层确认。
 * 保留24个三窗边界音频ID，不把它误作本窗最大确认源数。MIC0尾项作为完整性标志。 */
int m16_output_pool_audio_begin(m16_output_pool *,const m16_cont_info *,const double *,const double *,unsigned samples,uint64_t now);
int m16_output_pool_audio_copied(const m16_output_pool *,const m16_cont_info **);
int m16_output_pool_audio_release(m16_output_pool *,uint32_t epoch,uint32_t window,uint32_t source_id);
/* 原push/finish全部成功后才提交音频事务；收到MIC0尾项还不等于整个计算成功。 */
int m16_output_pool_audio_commit(m16_output_pool *,uint32_t epoch,uint32_t window,uint64_t now);
/* 一次至多复制32对音频、推进一个热图归约步和一个音频编码步。没有同步整窗发送。 */
int m16_output_pool_step(m16_output_pool *,uint64_t now);
/* 返回1=当前稳定只读报文；0=尚无报文；负数=故障。
 * 网络返回反压时不ack。已发送/深复制后ack；失败则先stop本层，再停止计算图。 */
int m16_output_pool_packet(m16_output_pool *,uint64_t now,m16_output_packet_view *);
int m16_output_pool_packet_ack(m16_output_pool *,const m16_output_packet_ticket *,uint64_t now);
/* 只归还自己已经停止访问的CPU输出租约，不归还采集/计算的输入或硬件arena。 */
int m16_output_pool_stop(m16_output_pool *);
int m16_output_pool_releasable(const m16_output_pool *);
/* scheduler已在全部硬件租约归还后重启；本层要求同一个更大epoch。 */
int m16_output_pool_restart(m16_output_pool *,uint32_t new_epoch);
#ifdef __cplusplus
}
#endif
#endif
