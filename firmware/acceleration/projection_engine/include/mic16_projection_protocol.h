#ifndef MIC16_PROJECTION_PROTOCOL_H
#define MIC16_PROJECTION_PROTOCOL_H
#include <stdint.h>
/* 默认保留V3基线；V5固件必须由匹配平台构建显式选择。 */
#ifndef M16_PROJECTION_VERSION
#define M16_PROJECTION_VERSION 3u
#endif
#if M16_PROJECTION_VERSION == 3u
#define M16_PROJECTION_EXPECTED_ABI 2026102403u
#elif M16_PROJECTION_VERSION == 5u
#define M16_PROJECTION_EXPECTED_ABI 2026102705u
#elif M16_PROJECTION_VERSION == 6u
#define M16_PROJECTION_EXPECTED_ABI 2026102806u
#else
#error 不支持的共享计算协议版本
#endif
#ifndef M16_PROJECTION_BUILD_ABI
#define M16_PROJECTION_BUILD_ABI M16_PROJECTION_EXPECTED_ABI
#elif M16_PROJECTION_BUILD_ABI != M16_PROJECTION_EXPECTED_ABI
#error 共享计算协议版本与ABI不匹配
#endif
#define M16_PROJECTION_JOBS 8u
#define M16_PROJECTION_DESCRIPTOR_WORDS 8u
#define M16_PROJECTION_MAX_WORDS 2097152u
typedef struct {
    uint64_t tag;
    uint32_t mode,obs,coef,out,report,frames,bins,count,no,nc,nr;
} m16_projection_job;
#ifdef __cplusplus
extern "C" {
#endif
/* 全部描述符的完整PS预检：维度、派生跨度、保留位、范围、跨任务读写别名与重复标签。
 * plan须位于arena之外；失败不改变arena，plan可能包含尚未完成的临时字段。
 * 通过后调用方须持有整个arena的独占租约，直到硬件idle及结果失效缓存完成。
 * 这是新硬件版本3的必经入口，禁止直接把未经此预检的数据交给计算IP。 */
int m16_projection_validate(const uint64_t *arena,unsigned words,unsigned descriptors,
                             unsigned jobs,m16_projection_job *plan);
#ifdef __cplusplus
}
#endif
#endif
