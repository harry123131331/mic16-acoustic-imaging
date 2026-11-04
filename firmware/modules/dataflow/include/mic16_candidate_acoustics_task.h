#ifndef MIC16_CANDIDATE_ACOUSTICS_TASK_H
#define MIC16_CANDIDATE_ACOUSTICS_TASK_H
#include "mic16_candidate_beam_task.h"
#include "mic16_mono_feature_task.h"
#include "mic16_physical.h"
enum {M16_CA_IDLE=0,M16_CA_BEAM=1,M16_CA_MONO=2,M16_CA_REFERENCE_BEAM=3,M16_CA_REFERENCE_LEVELS=4,
 M16_CA_COMPLETE=5,M16_CA_STOPPING=6,M16_CA_STOPPED=7,M16_CA_FAILED=8};
enum {M16_CA_INVALID=-600,M16_CA_BUSY=-601};
typedef struct {
 unsigned magic,phase,guard,reference_guard;int error;
 uint64_t epoch,first_job;
 double direction[2],reference_floor;
 const float *pcm,*reference;
 mic16_physical_workspace *work;
 m16_shared_spectrum_task *device;
 m16_candidate_beam_task beam;
 m16_mono_feature_task mono;
} m16_candidate_acoustics_task;
/* 初始化后按候选复用；work与task/FFT设备缓冲分离，spectral已初始化。 */
int m16_candidate_acoustics_init(m16_candidate_acoustics_task *,m16_shared_spectrum_task *,mic16_physical_workspace *);
/* 真实16路PCM至候选频带/能量的完整子图：波束→20帧FFT/频带→参考波束→4帧能量。
 * 不新采参考，不负责候选选择、物理响应24列、统计特征、评分或增强。
 * PCM/ref与work/device/task及彼此分离；两批FFT使用first_job与first_job+1。
 * 输出work.bands/beam_level/quiet_level，以及task.guard/reference_floor，仅COMPLETE有效。 */
int m16_candidate_acoustics_begin(m16_candidate_acoustics_task *,const float *,unsigned pcm_values,
 const float *reference,unsigned reference_values,const double *direction,uint64_t epoch,uint64_t first_job);
int m16_candidate_acoustics_step(m16_candidate_acoustics_task *);
int m16_candidate_acoustics_stop(m16_candidate_acoustics_task *);
int m16_candidate_acoustics_releasable(const m16_candidate_acoustics_task *);
#endif
