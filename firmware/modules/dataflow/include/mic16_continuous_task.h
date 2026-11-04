#ifndef MIC16_CONTINUOUS_TASK_H
#define MIC16_CONTINUOUS_TASK_H
#include "mic16_continuous.h"
#include "mic16_complex_fft_task.h"
#include "mic16_resample_task.h"
enum {M16_CT_IDLE=0,M16_CT_VALIDATE_PCM=1,M16_CT_VALIDATE_MASK=2,M16_CT_VALIDATE_WEIGHTS=3,
 M16_CT_COPY_PCM=4,M16_CT_COPY_PREDICTION=5,M16_CT_EMIT_SETUP=6,M16_CT_SOURCE=7,M16_CT_RAW_CLEAR=8,
 M16_CT_RAW_BLOCK=9,M16_CT_RAW_BEAM=10,M16_CT_RAW_REFERENCE=11,M16_CT_RESAMPLE=12,M16_CT_OLA_CLEAR=13,
 M16_CT_MASKS=14,M16_CT_FRAME=15,M16_CT_FFT=16,M16_CT_MULTIPLY=17,M16_CT_CONJUGATE=18,
 M16_CT_IFFT=19,M16_CT_OLA=20,M16_CT_CROP=21,M16_CT_OUTPUT_READY=22,M16_CT_COMPLETE=23,
 M16_CT_STOPPED=24,M16_CT_FAILED=25};
enum {M16_CT_INVALID=-920,M16_CT_BUSY=-921,M16_CT_INPUT=-922,M16_CT_OUTPUT=-923,M16_CT_FLOAT_ENV=-924};
typedef struct {int delay[16];float coefficient[16][6];} m16_cont_steering;
typedef struct {
 unsigned magic,phase,cursor,mask_source,prediction_count,weight_frame,final,index,first,id_count,ordinal;
 unsigned raw_count,length,frames,total,frame,samples,selection,side,channel,last_items,max_items;
 int error,prediction_index,old_index,result_count;
 uint32_t ids[24];
 uint64_t steps,origin,stop,n,block,block_stop;
 double value[2],masks[64];
 m16_cont_steering steering[2];
 const m16_cont_prediction *now,*old,*input_prediction;
 const float *input;
 m16_cont_workspace *work;
 m16_cont_info info;
 m16_complex_fft_task fft;
 m16_resample_task resample;
} m16_continuous_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 常驻初始化。只准备原FFT表和控制数据；三窗历史及音频工作区在DDR，不放任务栈。 */
int m16_continuous_task_init(m16_continuous_task *,m16_cont_workspace *,uint32_t epoch);
/* PCM和预测输入与task及整个work分离，借用至COMPLETE；first_sample必须连续。
 * 第二窗起每次输出前一窗。0号是MIC0参考，其他ID仍使用原三窗支持区并集，最多24个边界ID。
 * 每次只开始一次push/finish；没有同步整窗重建或同步回调。 */
int m16_continuous_task_push(m16_continuous_task *,const float *,const m16_cont_prediction *,uint64_t first_sample);
int m16_continuous_task_finish(m16_continuous_task *);
/* 1继续（包含等待音频消费者），0本次完成，2停止，负数错误；不表示实际实时截止期。 */
int m16_continuous_task_step(m16_continuous_task *);
/* 音频就绪后只读借用，消费者复制或完全消费后按epoch/window/source_id释放。
 * 同一窗口全部输出在COMPLETE前都是临时事务，失败/取消必须整窗作废。 */
int m16_continuous_task_output(const m16_continuous_task *,const m16_cont_info **,const double **,const double **,unsigned *samples);
int m16_continuous_task_release(m16_continuous_task *,uint32_t epoch,uint32_t window,uint32_t source_id);
int m16_continuous_task_stop(m16_continuous_task *);
int m16_continuous_task_releasable(const m16_continuous_task *);
/* 停止/非法输入会使当前时段失败。必须用不同非零epoch复位，禁止延用部分历史。 */
int m16_continuous_task_reset(m16_continuous_task *,uint32_t epoch);
#ifdef __cplusplus
}
#endif
#endif
