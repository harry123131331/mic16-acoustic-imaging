#ifndef MIC16_CAPTURE_SESSION_H
#define MIC16_CAPTURE_SESSION_H
#define M16_SESSION_REFERENCE_PACKETS 512u
#define M16_SESSION_AUDIO_PACKETS 7680u
#define M16_SESSION_TOTAL_PACKETS 8192u
#define M16_SESSION_WARMUP_PACKETS 256u
enum { M16_SESSION_QUIET=1,M16_SESSION_WAIT_AUDIO=2,M16_SESSION_AUDIO=3,
    M16_SESSION_DONE=4,M16_SESSION_FAILED=5 };
typedef struct {
    unsigned int phase,stored,discarded,warmup,received,early_start,audio_first_sequence;
} mic16_capture_session;
void mic16_session_init(mic16_capture_session *);
/* 仅完整DMA包通过传输、缓存和保护检查后调用。A命令在安静参考完成后启动采集，X在包边界取消。返回1表示保存，0丢弃，2保存末包，-1停止或无效。序号仅为本地成功接收记账，不证明物理采样连续性。 */
int mic16_session_packet(mic16_capture_session *,int command,unsigned int *slot);
#endif
