#ifndef MIC16_OUTPUT_WIRE_TASK_H
#define MIC16_OUTPUT_WIRE_TASK_H
#include "mic16_source_wire.h"
#include <stdint.h>

enum {M16_OW_IDLE=0,M16_OW_VALIDATE=1,M16_OW_HEADER=2,M16_OW_PAYLOAD=3,
 M16_OW_CRC_HEADER=4,M16_OW_CRC_BODY=5,M16_OW_READY=6,M16_OW_COMPLETE=7,
 M16_OW_STOPPED=8,M16_OW_FAILED=9,M16_OW_CLEAR=10,M16_OW_MEAN=11};
enum {M16_OW_MORE=0,M16_OW_PACKET=1,M16_OW_DONE=2,
 M16_OW_INVALID=-980,M16_OW_BUSY=-981,M16_OW_INPUT=-982,M16_OW_LEASE=-983,M16_OW_COUNTER=-984};
typedef struct {
 uintptr_t owner;
 uint64_t serial;
 uint32_t epoch,window,source_id;
} m16_wire_ticket;

/* 调用方独占CPU控制对象。输入及对象不得重叠，不在IRQ/lwIP回调中执行。
 * packet返回只读稳定借用；网络反压时不ack，下一轮重试同一字节序列。
 * ack仅在本包真正交给网络后调用。取消立即使借用失效，不冒充已发送。
 * 不执行UDP，不操作DMA/缓存；原始有效载荷及CRC与旧同步编码器保持。 */
typedef struct {
 unsigned magic,phase,index,offset,count,size,samples;
 uint32_t checksum;
 uint64_t serial;
 int error;
 m16_cont_info info;
 const float *interleaved;
 unsigned char packet[M16_SOURCE_WIRE_MAX];
} m16_audio_wire_task;
typedef struct {
 unsigned magic,phase,index,grid,offset,count,size;
 uint32_t epoch,window,checksum;
 uint64_t serial;
 int error;
 const float *maps;
 double sums[325];
 float values[325];
 unsigned char packet[1032];
} m16_heatmap_wire_task;

#ifdef __cplusplus
extern "C" {
#endif
/* 对象先清零，仅初次init；跨停止/错误后begin须使用更大epoch，序号不归零。 */
int m16_audio_wire_task_init(m16_audio_wire_task *);
/* interleaved为独立输出槽中的WDAS/增强F32交错对，不借用计算工作区。
 * 每步最多检查/打包32对或处理64个CRC字节，整源验证后才产生第一包。
 * info中MIC0参考在有声源时不发送；无源时保持原单个60字节公告。 */
int m16_audio_wire_task_begin(m16_audio_wire_task *,const m16_cont_info *,const float *interleaved,unsigned samples);
int m16_audio_wire_task_step(m16_audio_wire_task *);
int m16_audio_wire_task_packet(const m16_audio_wire_task *,const unsigned char **,unsigned *,m16_wire_ticket *);
int m16_audio_wire_task_ack(m16_audio_wire_task *,const m16_wire_ticket *);
int m16_audio_wire_task_stop(m16_audio_wire_task *);

int m16_heatmap_wire_task_init(m16_heatmap_wire_task *);
/* 全480×325原始空间响应，按原每格帧顺序FP64累加/480后转F32。
 * 每步至多64格，全部输入检查通过才发布。不是由方向点生成热图。 */
int m16_heatmap_wire_task_begin(m16_heatmap_wire_task *,uint32_t epoch,uint32_t window,const float *maps);
int m16_heatmap_wire_task_step(m16_heatmap_wire_task *);
/* 归约完成即可归还maps，网络发送使用独立values/packet，不拖住下一窗计算。 */
int m16_heatmap_wire_task_input_releasable(const m16_heatmap_wire_task *);
int m16_heatmap_wire_task_packet(const m16_heatmap_wire_task *,const unsigned char **,unsigned *,m16_wire_ticket *);
int m16_heatmap_wire_task_ack(m16_heatmap_wire_task *,const m16_wire_ticket *);
int m16_heatmap_wire_task_stop(m16_heatmap_wire_task *);
#ifdef __cplusplus
}
#endif
#endif
