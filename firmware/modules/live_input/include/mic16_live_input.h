#ifndef MIC16_LIVE_INPUT_H
#define MIC16_LIVE_INPUT_H
#include <stdint.h>
#define M16_LIVE_CHANNELS 16u
#define M16_LIVE_PACKET_BYTES 1024u
#define M16_LIVE_PACKET_SAMPLES 16u
#define M16_LIVE_REFERENCE_SAMPLES 8192u
#define M16_LIVE_WINDOW_SAMPLES 40960u
enum { M16_LIVE_OK=0, M16_LIVE_REFERENCE_READY=1, M16_LIVE_WINDOW_READY=2,
    M16_LIVE_INVALID=-1, M16_LIVE_BUSY=-2, M16_LIVE_STATE=-3,
    M16_LIVE_GAP=-4, M16_LIVE_RANGE=-5 };
enum { M16_LIVE_STOPPED=0, M16_LIVE_REFERENCE=1, M16_LIVE_AUDIO=2,
    M16_LIVE_READY=3 };
/* 不使用隐式默认值。除数为2^scale_exponent，不是SPL标定值。logical_to_wire必须是排列，context_id标识姿态/标定。要求单所有者及外部同步，不是中断或多核安全队列。 */
typedef struct {
    unsigned int scale_exponent;
    unsigned char logical_to_wire[M16_LIVE_CHANNELS];
    uint64_t context_id;
} mic16_live_config;
typedef struct {
    float *reference, *pcm;
    mic16_live_config config;
    unsigned int mode,reference_samples,pcm_samples,sequence_valid,leased;
    uint32_t next_sequence;
    uint64_t epoch,revision,leased_revision,clock_samples,window_start;
} mic16_live_input;
typedef struct {
    const float *reference, *pcm;
    uint64_t epoch,revision,context_id,start_sample,end_sample;
} mic16_live_view;
/* 调用方提供互不重叠的DDR数组，容量按float个数而不是字节计算，并与状态和配置分离；仅调用方拥有存储时允许初始化或配置。 */
int mic16_live_init(mic16_live_input *,const mic16_live_config *,
    float *reference,unsigned int reference_floats,float *pcm,unsigned int pcm_floats);
int mic16_live_configure(mic16_live_input *,const mic16_live_config *);
/* 显式采集安静参考，禁止根据幅值或源分数自动推断安静。 */
int mic16_live_begin_reference(mic16_live_input *);
int mic16_live_begin_audio(mic16_live_input *);
/* 停止或缺口在逻辑上作废借用权，不覆盖仍被借用的数组。姿态、通道、幅值或参考改变时invalidate_reference=1；仅独立参考仍适用时可为0。恢复必须显式进行，不补零。 */
int mic16_live_interrupt(mic16_live_input *,int invalidate_reference);
/* 只有IOC、DMA空闲、无错误且缓存已失效的数据包才可设置transport_ok=1。序号是接收记账，不能证明无物理丢样或溢出。BUSY不消费数据，调用方应保留/排队，丢弃则必须标记缺口。 */
int mic16_live_push(mic16_live_input *,const unsigned char *,unsigned int bytes,
    uint32_t sequence,int transport_ok);
int mic16_live_take(mic16_live_input *,mic16_live_view *);
int mic16_live_view_current(const mic16_live_input *,const mic16_live_view *);
int mic16_live_release(mic16_live_input *,const mic16_live_view *);
#endif
