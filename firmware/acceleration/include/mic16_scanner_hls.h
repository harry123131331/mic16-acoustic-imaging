#ifndef MIC16_SCANNER_HLS_H
#define MIC16_SCANNER_HLS_H
#include <stdint.h>
#include "mic16_spatial.h"
/* 旧扫描核的偏移单位为64位字，不是字节。保留现有BIT的寄存器合同。 */
#define M16_SCAN_OBS_WORDS 25824u
#define M16_SCAN_PHASE_OFFSET 25824u
#define M16_SCAN_STEP_OFFSET 36224u
#define M16_SCAN_OUTPUT_OFFSET 46624u
#define M16_SCAN_WORDS 50524u
#define M16_SCAN_PADDED_WORDS 50528u
enum {M16_SCAN_INVALID=-1,M16_SCAN_BUSY=-2,M16_SCAN_TIMEOUT=-3,
      M16_SCAN_GUARD=-4,M16_SCAN_CLOCK=-5};
typedef struct {
    void *context;
    uint32_t (*read)(void *,unsigned);
    void (*write)(void *,unsigned,uint32_t);
    void (*flush)(void *,const void *,unsigned);
    void (*invalidate)(void *,const void *,unsigned);
    uint64_t (*ticks)(void *);
    void (*barrier)(void *);
    uint32_t arena_address;
    uint64_t timeout_ticks;
} m16_scanner_io;
typedef struct {
    uint64_t before[8],words[M16_SCAN_PADDED_WORDS],after[8];
} m16_scanner_arena;
typedef struct {
    m16_scanner_arena arena;
    m16_scanner_io io;
    unsigned initialized,quarantined;
} m16_scanner_workspace;
typedef struct {
    uint64_t packing_ticks,device_ticks,decoding_ticks,total_ticks;
    unsigned completed_frames,last_control,quarantined;
} m16_scanner_report;
typedef struct {unsigned refined_grids,full_zones,zero_input;} m16_scanner_refinement;
int m16_scanner_quantize(float x,int32_t *raw);
uint64_t m16_scanner_pack(int32_t real,int32_t imag);
/* 首次使用须清零且64字节对齐；超时后隔离，复位硬件前不得再次init或重用内存。 */
int m16_scanner_init(m16_scanner_workspace *,const m16_scanner_io *);
int m16_scanner_project(m16_scanner_workspace *,const float *,float *,m16_scanner_report *);
/* 按原FP64求和顺序重算投票峰值，保护带只决定重算范围，不改检测门限。 */
int m16_scanner_refine(const float *,float *,mic16_spatial_workspace *,m16_scanner_refinement *);
/* 本入口只在ARM构建中定义，主机测试使用受控的寄存器/缓存替身。 */
int m16_scanner_platform_init(m16_scanner_workspace *);
#endif
