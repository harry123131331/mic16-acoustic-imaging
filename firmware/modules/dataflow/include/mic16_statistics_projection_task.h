#ifndef MIC16_STATISTICS_PROJECTION_TASK_H
#define MIC16_STATISTICS_PROJECTION_TASK_H
#include "mic16_extended.h"
#include "mic16_projection_runtime.h"
enum {M16_ST_IDLE=0,M16_ST_VALIDATE=1,M16_ST_GEOMETRY=2,M16_ST_SIGNATURES=3,M16_ST_PAIRS=4,
 M16_ST_CLEAR=5,M16_ST_PACK=6,M16_ST_COEFFICIENTS=7,M16_ST_SUBMIT=8,M16_ST_WAIT=9,
 M16_ST_CONSUME=10,M16_ST_RETIRE=11,M16_ST_CPU=12,M16_ST_ROWS=13,M16_ST_MEAN=14,
 M16_ST_VARIANCE=15,M16_ST_ADVANCE=16,M16_ST_TIME=17,M16_ST_FINAL_COPY=18,M16_ST_CHECK=19,
 M16_ST_DRAIN=20,M16_ST_COMPLETE=21,M16_ST_STOPPED=22,M16_ST_FAILED=23};
enum {M16_ST_INVALID=-640,M16_ST_BUSY=-641,M16_ST_INPUT=-642,M16_ST_OUTPUT=-643,
 M16_ST_FLOAT_ENV=-644,M16_ST_OWNERSHIP=-645};
#define M16_ST_ARENA_WORDS 40000u
#define M16_ST_PAIR_VALUES (4u*256u*32u*2u)
/* 第二份通道对只用于下一块预计算；bands保留原FP64频点累计次序。
 * 由调用方在DDR分配，共688128字节；不分配堆或整段PCM副本。 */
typedef struct {double pairs_next[M16_ST_PAIR_VALUES];double bands[4u*16u*64u*5u];} m16_statistics_projection_storage;
typedef struct {
 unsigned magic,phase,count,candidate,cursor,region,frame_start,frames,buffer,next_cursor,first_bin;
 unsigned obs,coef,out,receipt,used,max_used,lanes,owned,frame,zone,feature,time_frame,time_count;
 unsigned last_items,max_items,transactions,rotations,pair_normalizations,prefetch_pairs,device_prefetch_pairs,stop_requested;
 int error;
 uint64_t epoch,first_tag,last_epoch,reserved_tag_high,steps;
 float mean,variance,time_sum;
 float arrivals[16];double directions[32];
 float *output;
 mic16_extended_workspace *work;
 m16_statistics_projection_storage *storage;
 m16_projection_runtime *runtime;
} m16_statistics_projection_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 首次零对象，work、storage、task与runtime/arena互不重叠。
 * 输入是已经完成的observed[67,257,16,2]及beam/channel/quiet功率，不是原始PCM。
 * begin前物理88维节点必须安全释放PHAT读租约；本节点会改写其union中的统计缓存。 */
int m16_statistics_projection_init(m16_statistics_projection_task *,m16_projection_runtime *,mic16_extended_workspace *,m16_statistics_projection_storage *);
/* 只读频谱/功率至COMPLETE；1候选沿用原FP64旋转归约，2～16候选用共享核模式1。
 * 输出[C,20,18]，可为work.statistics18或完全独立缓冲；不能别名其它工作区字段。
 * 预留136个标签（17个四帧块×8个32频点块），取消也不复用；不是物理时间戳。
 * 所有部分输出无效直到COMPLETE；本节点不计算重采样、STFT或候选波束功率。 */
int m16_statistics_projection_begin(m16_statistics_projection_task *,const double *,unsigned count,float *,unsigned values,uint64_t epoch,uint64_t first_tag);
/* 有限工作片段：64项验证/复制、32对归一化、8个签名，或一次后端poll。
 * WAIT可同时预计算下一块32对；硬件输出每步消费16组，保留频点累计次序。 */
int m16_statistics_projection_step(m16_statistics_projection_task *);
int m16_statistics_projection_stop(m16_statistics_projection_task *);
int m16_statistics_projection_releasable(const m16_statistics_projection_task *);
#ifdef __cplusplus
}
#endif
#endif
