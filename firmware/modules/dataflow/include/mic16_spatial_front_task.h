#ifndef MIC16_SPATIAL_FRONT_TASK_H
#define MIC16_SPATIAL_FRONT_TASK_H
#include "mic16_public_spectrum_task.h"
#include "mic16_coarse_scan_runtime.h"
#include "mic16_grid_refinement_task.h"
enum {M16_SF_IDLE=0,M16_SF_ACTIVE=1,M16_SF_REFINE=2,M16_SF_COMPLETE=3,
 M16_SF_STOPPING=4,M16_SF_STOPPED=5,M16_SF_FAILED=6};
enum {M16_SF_INVALID=-460,M16_SF_BUSY=-461,M16_SF_SEQUENCE=-462};
typedef struct {
 unsigned magic,phase,front_phase,front_index,ready,submitted,completed,cursor,turn,stop_requested;
 int error;
 uint64_t epoch,first_sample,first_frame,first_tag,steps,front_steps_while_device;
 const float *pcm;
 float *phat,*maps;
 double *levels;
 m16_coarse_scan_runtime *coarse;
 m16_public_spectrum_task spectrum;
 m16_grid_refinement_task refinement;
 m16_pp_grid_cache *evidence_cache;
} m16_spatial_front_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 实际新任务子图：公共FP64频谱→PHAT/电平→逐帧粗扫→分块FP64精算。
 * coarse的相位表必须已分步初始化，两个PL引擎的CPU/物理arena都须独立。
 * 两个驱动、FFT工作区、帧暂存及本节点不得与其它活动节点复用。 */
/* 绑定后粗扫设备由子帧前端拥有；本节点只借用已快照的粗图和PHAT。 */
int m16_spatial_front_bind_evidence(m16_spatial_front_task *,m16_pp_grid_cache *);
int m16_spatial_front_begin_evidence(m16_spatial_front_task *,const float *,unsigned,float *,unsigned,
 double *,unsigned,float *,unsigned,uint64_t,uint64_t,uint64_t);
int m16_spatial_front_init(m16_spatial_front_task *,mic16_spectral_frame *,mic16_spectral_workspace *,
                           m16_coarse_scan_runtime *,m16_projection_runtime *);
/* PCM[40960,16]、PHAT[20,807,32,2]、电平[20,24]、空间图[20,24,325]。
 * 所有输入/工作/输出租约保持到releasable为真。first_sample是本epoch逻辑样本
 * 序号且须位于40960边界，不把它或设备回执时间当作已校准物理时间。 */
int m16_spatial_front_begin(m16_spatial_front_task *,const float *,unsigned pcm_values,
 float *,unsigned phat_values,double *,unsigned level_values,float *,unsigned map_values,
 uint64_t epoch,uint64_t first_sample,uint64_t first_tag);
/* 已由同一上下文前端准备好PHAT和电平，仅运行原粗扫与精算，不重复FFT。
 * 只能由持有输入写/读租约的调用者使用；数组布局、精度、时间顺序不变。 */
int m16_spatial_front_begin_prepared(m16_spatial_front_task *,const float *,unsigned,
 float *,unsigned,double *,unsigned,float *,unsigned,uint64_t,uint64_t,uint64_t);
/* 1进行中、0该子图整窗完成、2停止、负数故障。交替推进有限CPU片段和PL驱动；
 * PL粗扫当前帧时，CPU可算下一帧。没有旧整窗同步调用，也没有阻塞等待。 */
int m16_spatial_front_step(m16_spatial_front_task *);
int m16_spatial_front_stop(m16_spatial_front_task *);
/* 停止/错误不自动证明工作区安全；任何驱动隔离后本函数为假，禁止覆盖缓冲。 */
int m16_spatial_front_releasable(const m16_spatial_front_task *);
#ifdef __cplusplus
}
#endif
#endif
