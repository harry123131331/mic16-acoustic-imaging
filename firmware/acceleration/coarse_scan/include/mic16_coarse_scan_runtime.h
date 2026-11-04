#ifndef MIC16_COARSE_SCAN_RUNTIME_H
#define MIC16_COARSE_SCAN_RUNTIME_H
#include "mic16_scanner_hls.h"
enum {M16_CS_IDLE=0,M16_CS_TABLES=1,M16_CS_TABLE_FLUSH=2,M16_CS_PACK=3,M16_CS_FILL=4,
 M16_CS_FLUSH=5,M16_CS_START=6,M16_CS_DEVICE=7,M16_CS_INVALIDATE=8,M16_CS_DECODE=9,
 M16_CS_COMPLETE=10,M16_CS_STOPPED=11,M16_CS_SAFE_ERROR=12,M16_CS_QUARANTINE=13};
typedef struct {
 uint64_t prepare_ticks,device_ticks,collect_ticks,total_ticks;
 unsigned flush_calls,invalidate_calls,last_control,error;
} m16_coarse_scan_report;
/* 独占一帧粗扫arena；静态相位表只在常驻启动时准备/清缓存一次。
 * 不更改当前Q2.24核、地址或采样参数。控制事务仍是单帧全部24频带。
 * 设备没有硬件标签寄存器；epoch/frame是驱动持有的提交元数据，不能冒充
 * 硬件回执或物理采样时间。调度器须负责采样序号、连续性和工作区租约。 */
typedef struct {
 m16_scanner_arena arena;
 m16_scanner_io io;
 unsigned magic,phase,cursor,cache_stage,cache_cursor,done_seen,started,stop_requested,initialized;
 uint64_t epoch,frame,last_epoch,last_frame,last_tick,begin_tick,submitted_tick,collect_tick;
 const float *input;
 float *maps;
 m16_coarse_scan_report report;
} m16_coarse_scan_runtime __attribute__((aligned(64)));
#ifdef __cplusplus
extern "C" {
#endif
/* 首次为零对象。init仅绑定和检查idle，之后poll分步构造原相位表；
 * poll返回0且available为真才可提交首帧。只用于清单确认的原粗扫控制口。 */
int m16_coarse_scan_init(m16_coarse_scan_runtime *,const m16_scanner_io *);
int m16_coarse_scan_available(const m16_coarse_scan_runtime *);
/* 单帧PHAT[807,32,2]，输出maps[24,325]。submit不打包或循环等设备。
 * 同epoch帧号严格递增；新epoch从frame0开始。begin后不得覆盖输入/输出。 */
int m16_coarse_scan_submit(m16_coarse_scan_runtime *,const float *,unsigned input_values,
                          float *,unsigned map_values,uint64_t epoch,uint64_t frame);
/* 1进行中、0初始化/帧结果完成、2停止、负数错误。每步最多64个普通数据项、
 * 16个初始相位项或一次1KiB缓存维护/硬件检查。 */
int m16_coarse_scan_poll(m16_coarse_scan_runtime *);
/* COMPLETE/STOPPED/未启动的SAFE_ERROR后显式归还；QUARANTINE不能归还。 */
int m16_coarse_scan_retire(m16_coarse_scan_runtime *);
/* 1提交前已取消，0须继续poll排空；不重置硬件，不中断其它设备。 */
int m16_coarse_scan_cancel(m16_coarse_scan_runtime *);
#ifdef __cplusplus
}
#endif
#endif
