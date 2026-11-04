#ifndef MIC16_STATISTICS_TILE_RUNTIME_H
#define MIC16_STATISTICS_TILE_RUNTIME_H
#include "mic16_scanner_hls.h"
#include "mic16_statistics_tile_batch.h"
enum {M16_TILE_WORDS=2304,M16_TILE_BUILD_ABI=20261024};
enum {M16_TILE_INVALID=-201,M16_TILE_BUSY=-202,M16_TILE_TIMEOUT=-203,
      M16_TILE_CLOCK=-204,M16_TILE_GUARD=-205,M16_TILE_RETURN=-206,M16_TILE_ABI=-207};
typedef struct {
    uint64_t cache_ticks,device_ticks,invalidate_ticks,total_ticks;
    unsigned batches,last_control,quarantined;
    int kernel_return;
} m16_tile_runtime_report;
/* 首次使用清零且64字节对齐；超时/内存异常后隔离，硬件复位前不得重用。
 * io.arena_address必须对应words的物理地址。仅限已验证BIT/XSA摘要的候选包。
 * ABI常量和寄存器回读不构成硅片身份认证，禁止用于猜测旧WDAS核是否兼容。 */
typedef struct {
    uint64_t before[8],words[M16_TILE_WORDS],after[8];
    m16_scanner_io io;
    m16_tile_runtime_report report;
    uint64_t started,submitted,last_tick;
    unsigned initialized,quarantined,busy,observations_clean,done_seen,frames,bins,candidates;
} m16_tile_runtime;
int m16_tile_runtime_init(m16_tile_runtime *,const m16_scanner_io *,unsigned verified_build_abi);
int m16_tile_runtime_available(void *);
/* submit只提交；poll返回1表示进行中、0完成、负值失败。 */
int m16_tile_runtime_submit(m16_tile_runtime *,unsigned,unsigned,unsigned,unsigned);
int m16_tile_runtime_poll(m16_tile_runtime *);
int m16_tile_runtime_execute(void *,unsigned,unsigned,unsigned,unsigned);
int m16_tile_runtime_bind(m16_tile_runtime *);
int m16_tile_runtime_platform_init(m16_tile_runtime *,unsigned verified_build_abi);
#endif
