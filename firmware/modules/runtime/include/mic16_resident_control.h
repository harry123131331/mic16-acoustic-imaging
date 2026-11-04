#ifndef MIC16_RESIDENT_CONTROL_H
#define MIC16_RESIDENT_CONTROL_H
#include <stddef.h>
#include <stdint.h>

/* 新数据流架构的常驻控制。接收回调只交接命令；本模块不调用旧整窗compute。 */
#define M16_CONTROL_COMMAND_BYTES 32u
#define M16_CONTROL_STATUS_BYTES 80u
#define M16_CONTROL_UNKNOWN UINT64_MAX
enum {M16_CTL_HELLO=1,M16_CTL_STATUS=2,M16_CTL_START=3,M16_CTL_STOP=4};
enum {M16_CTL_IDLE=0,M16_CTL_STARTING=1,M16_CTL_RUNNING=2,M16_CTL_DRAINING=3,M16_CTL_FAULT=4};
enum {M16_CTL_CONTINUOUS=1,M16_CTL_VIDEO=2,M16_CTL_SOURCE_AUDIO=4,M16_CTL_TELEMETRY=8,M16_CTL_LOCALIZATION=16,M16_CTL_RAW_PCM=32,M16_CTL_SINGLE_CAPTURE=64,M16_CTL_FAST_DISPLAY=128};
enum {M16_CTL_DMA_BUSY=1,M16_CTL_PL_BUSY=2,M16_CTL_CPU_BUSY=4,M16_CTL_OUTPUT_BUSY=8};
enum {M16_CTL_INVALID=-180,M16_CTL_STALE_BOOT=-181,M16_CTL_UNSUPPORTED=-182,
      M16_CTL_BUSY=-183,M16_CTL_REPLAY=-184,M16_CTL_COUNTER=-185,M16_CTL_CLOCK=-186,
      M16_CTL_TRANSITION=-187,M16_CTL_NOT_QUIESCENT=-188};
typedef struct {
    uint32_t state,epoch,busy;
    int32_t code;
    uint64_t window,compute_us,queued_windows;
} m16_control_progress;
typedef struct {
    uint32_t magic,boot,capabilities,state,epoch,action,cache_valid,last_request;
    int32_t code;
    uint64_t nonce,window,compute_us,queued_windows,last_now_ms;
    uint64_t requests,duplicates,rejected,starts,stops;
    unsigned char cached_command[32],cached_reply[80];
} m16_resident_control;

/* boot必须由本次启动提供非零标识；不能把编译版本号当作每次启动的唯一编号。
 * 只初始化全新零对象；运行中的DMA/PL租约未归还前，禁止靠重新初始化恢复。 */
int m16_control_init(m16_resident_control *,uint32_t boot,uint32_t capabilities);
/* 由单一主循环所有者调用。成功写80字节回复；非法数据包不产生回复。
 * START仅登记动作并报告STARTING，STOP仅登记排空，不在接收处理内运行算法。
 * 同nonce/编号的重复命令重放同一回复，不重复启动；编号冲突或回退被拒绝。 */
int m16_control_handle(m16_resident_control *,const unsigned char *,unsigned,
                      unsigned char *,unsigned,uint64_t now_ms);
/* 主循环领取动作；实际完成状态必须通过update由新调度器确认。 */
unsigned m16_control_take_action(m16_resident_control *);
/* busy是调度器对DMA、共享PL、CPU工作区和输出槽的实际检查结果。
 * DRAINING→IDLE必须全部空闲；该接口不停止设备，也不代替实际空闲检查。 */
int m16_control_update(m16_resident_control *,const m16_control_progress *);
size_t m16_control_size(void);
size_t m16_control_alignment(void);
#endif
