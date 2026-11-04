/* 联合测试连接两个真实驱动/核模型的独立设备区域；原函数只用于生成参考。 */
#include "mic16_spatial_front_task.h"
#include <string.h>
extern int cs_reset(unsigned);
extern int cs_poll(void);
extern void *cs_runtime_bridge(void);
extern void *pp_runtime_bridge(unsigned);
extern int cs_reference(const float *,float *);
extern int rg_reference(const float *,float *,unsigned *);
extern int m16_spectral_process_real(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
static m16_spatial_front_task task;
static mic16_spectral_frame frame,reference_frame;
static mic16_spectral_workspace work,reference_work;
int sf_reset(const float *pcm,float *phat,double *levels,float *maps,uint64_t epoch,uint64_t first,uint64_t tag,unsigned overlap_physical){
 int rc=cs_reset(4);unsigned i;
 if(rc)return rc;
 for(i=0;i<2000;i++){rc=cs_poll();if(rc<=0)break;}
 if(rc||i==2000)return -999;
 memset(&task,0,sizeof(task));mic16_spectral_init(&work);
 m16_projection_runtime *p=(m16_projection_runtime *)pp_runtime_bridge(overlap_physical?0x02000000u:0x04000000u);
 if(!p)return -999;
 rc=m16_spatial_front_init(&task,&frame,&work,(m16_coarse_scan_runtime *)cs_runtime_bridge(),p);
 if(!rc)rc=m16_spatial_front_begin(&task,pcm,40960u*16u,phat,M16_SP_PHAT_FLOATS,levels,20u*24u,maps,M16_SP_MAP_FLOATS,epoch,first,tag);
 return rc;
}
int sf_begin(const float *pcm,float *phat,double *levels,float *maps,uint64_t epoch,uint64_t first,uint64_t tag){
 return m16_spatial_front_begin(&task,pcm,40960u*16u,phat,M16_SP_PHAT_FLOATS,levels,20u*24u,maps,M16_SP_MAP_FLOATS,epoch,first,tag);
}
int sf_step(void){return m16_spatial_front_step(&task);}
int sf_stop(void){return m16_spatial_front_stop(&task);}
int sf_releasable(void){return m16_spatial_front_releasable(&task);}
int sf_reference(const float *pcm,float *phat,double *levels,float *maps,unsigned *report){
 unsigned t;int rc;mic16_spectral_init(&reference_work);
 for(t=0;t<20;t++){
  rc=m16_spectral_process_real(pcm+t*2048u*16u,&reference_frame,&reference_work);if(rc)return rc;
  memcpy(phat+t*807u*32u*2u,reference_frame.phat,sizeof(reference_frame.phat));
  memcpy(levels+t*24u,reference_frame.zone_level_db,sizeof(reference_frame.zone_level_db));
  rc=cs_reference(reference_frame.phat,maps+t*24u*325u);if(rc)return rc;
 }
 return rg_reference(phat,maps,report);
}
uint64_t sf_metric(unsigned i){
 switch(i){case 0:return task.phase;case 1:return task.front_index;case 2:return task.ready;
 case 3:return task.submitted;case 4:return task.completed;case 5:return task.front_steps_while_device;
 case 6:return task.steps;case 7:return task.refinement.report.refined_grids;case 8:return task.refinement.report.full_zones;
 case 9:return task.refinement.phase;case 10:return task.front_phase;case 11:return task.epoch;
 case 12:return task.first_sample;case 13:return sizeof(task);default:return 0;}
}
