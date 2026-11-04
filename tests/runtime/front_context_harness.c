/* 复用已经核验的FFT控制注入及原特征参考包装，新增真实公共前端与粗扫。 */
#include "feature_pipeline_harness.c"
#include "mic16_detection_task.h"
extern int cs_reset(unsigned),cs_poll(void),cs_hardware(unsigned),cs_reference(const float *,float *);
extern void *cs_runtime_bridge(void);
extern uint64_t cs_metric(unsigned);
extern int m16_spectral_process_real(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
extern int m16_scanner_refine_accumulators(const float *,float *,mic16_spatial_workspace *,m16_scanner_refinement *);
static m16_detection_task detection;
static mic16_pipeline_workspace detection_work;
static mic16_pipeline_observation detection_output;
static m16_statistics_projection_storage detection_storage;
static mic16_spatial_workspace expected_spatial;
static mic16_pool expected_pool;
static mic16_score_workspace expected_score;
static float expected_maps[M16_SP_MAP_FLOATS],expected_logits[16];
static double expected_votes[480*9],expected_scores[16];
static unsigned short expected_indices[480];
static unsigned coarse_age,real_coarse_calls;
int dt_begin(uint64_t epoch,uint64_t first,uint64_t job,uint64_t tag){
 int rc=m16_detection_begin(&detection,pcm,40960u*16u,quiet,8192u*16u,&detection_output,epoch,first,job,tag);
 if(!rc)base=served;
 return rc;
}
int dt_setup_ready(const float *x,const float *q,unsigned overlap){
 unsigned i;int rc=ss_reset(0);if(rc)return rc;
 rc=cs_reset(4);if(rc)return rc;
 for(i=0;i<2000u;i++){rc=cs_poll();if(rc<=0)break;}
 if(rc || i==2000u)return -999;
 runtime=(m16_projection_runtime *)pp_runtime_bridge(overlap==1u?0x02000000u:(overlap==2u?0x04000000u:0x06000000u));if(!runtime)return -999;
 memset(&detection,0,sizeof(detection));memset(&detection_work,0,sizeof(detection_work));memset(&detection_storage,0,sizeof(detection_storage));
 memset(&reference,0,sizeof(reference));memset(&expected_spatial,0,sizeof(expected_spatial));memset(&expected_score,0,sizeof(expected_score));memset(&expected_pool,0,sizeof(expected_pool));
 mic16_spectral_init(&detection_work.extended.physical.spectral);mic16_spectral_init(&reference.physical.spectral);
 pcm=x;quiet=q;calls=ready=served=base=checks=projection_starts=device_age=coarse_age=real_coarse_calls=0;
 memset(&detection_output,0,sizeof(detection_output));
 for(i=0;i<16u*20u*130u;i++)detection_output.features[i]=-987.25f;
 rc=m16_detection_init(&detection,&detection_work,(m16_coarse_scan_runtime *)cs_runtime_bridge(),ss_task_bridge(),runtime,&detection_storage);
 if(rc)return rc;
 return 0;
}
int dt_reference(void){
 unsigned i,t;int rc;m16_scanner_refinement report;
 mic16_reference_init(&reference.physical.reference);
 for(t=0;t<4u;t++){
  rc=m16_spectral_process_real(quiet+t*2048u*16u,&reference.physical.frame,&reference.physical.spectral);if(rc)return rc;
  rc=mic16_reference_add(&reference.physical.reference,reference.physical.frame.zone_level_db);if(rc)return rc;
 }
 rc=mic16_reference_finalize(&reference.physical.reference);if(rc)return rc;
 for(t=0;t<20u;t++){
  rc=m16_spectral_process_real(pcm+t*2048u*16u,&reference.physical.frame,&reference.physical.spectral);if(rc)return rc;
  memcpy(reference.physical.phat+t*807u*32u*2u,reference.physical.frame.phat,sizeof(reference.physical.frame.phat));
  memcpy(reference.physical.levels+t*24u,reference.physical.frame.zone_level_db,24u*8u);
  rc=cs_reference(reference.physical.frame.phat,expected_maps+t*24u*325u);if(rc)return rc;
 }
 rc=m16_scanner_refine_accumulators(reference.physical.phat,expected_maps,&expected_spatial,&report);if(rc)return rc;
 rc=mic16_spatial_votes(expected_maps,reference.physical.levels,reference.physical.reference.floor_db,expected_votes,expected_indices);if(rc)return rc;
 rc=mic16_spatial_candidates(expected_votes,&expected_pool,&expected_spatial);if(rc)return rc;
 count=expected_pool.count;votes=expected_votes;
 for(i=0;i<32u;i++)directions[i]=0.;
 for(i=0;i<count;i++){directions[i*2u]=expected_pool.items[i].direction[0];directions[i*2u+1u]=expected_pool.items[i].direction[1];}
 rc=fp_reference();if(rc)return rc;
 rc=mic16_frozen_score(expected,expected_model,expected_bits,expected_logits,&expected_score);if(rc)return rc;
 for(i=0;i<16u;i++)expected_scores[i]=mic16_frozen_probability(expected_logits[i]);
 return 0;
}
int dt_service(unsigned actual){
 int rc;
 if(cs_metric(0)==M16_CS_DEVICE && cs_metric(10)==0u){
  if(++coarse_age>=8u){rc=cs_hardware(actual);if(rc)return rc;coarse_age=0;if(actual)real_coarse_calls++;}
 }
 return fp_service(0,0);
}
int dt_step(void){return m16_detection_step(&detection);}
int dt_run(unsigned limit,unsigned phase,unsigned actual){
 unsigned i;int rc=1;
 for(i=0;i<limit && rc==1;i++){
  if(phase && detection.phase==phase)return 1;
  rc=dt_service(actual);if(rc<0)return rc;
  rc=dt_step();
 }
 return rc;
}
int dt_until_coarse_device(unsigned limit){
 unsigned i;
 for(i=0;i<limit;i++){
  int rc;if(cs_metric(0)==M16_CS_DEVICE)return 1;
  rc=dt_service(0);if(rc<0)return rc;
  rc=dt_step();if(rc!=1)return rc;
 }
 return -999;
}
int dt_stop(void){return m16_detection_stop(&detection);}
int dt_releasable(void){return m16_detection_releasable(&detection);}
float *dt_features(unsigned old){return old?expected:detection_output.features;}
float *dt_logits(unsigned old){return old?expected_logits:detection_output.logits;}
double *dt_scores(unsigned old){return old?expected_scores:detection_output.scores;}
double *dt_directions(unsigned old){return old?expected_model:detection_output.directions;}
unsigned char *dt_valid(unsigned old){return old?expected_bits:detection_output.valid;}
float *dt_maps(unsigned old){return old?expected_maps:detection_work.maps;}
double *dt_votes(unsigned old){return old?expected_votes:detection_work.votes;}
unsigned short *dt_indices(unsigned old){return old?expected_indices:detection_work.best_indices;}
int dt_pool_equal(void){return memcmp(&detection_work.pool,&expected_pool,sizeof(expected_pool))==0;}
uint64_t dt_metric(unsigned k){
 switch(k){case 0:return detection.phase;case 1:return detection.steps;case 2:return detection_output.candidate_count;case 3:return expected_pool.count;
 case 4:return detection.reference_frame;case 5:return detection.front.completed;case 6:return detection.front.refinement.batches;
 case 7:return real_coarse_calls;case 8:return checks;case 9:return detection.reserved_tag_high;case 10:return detection.reserved_job_high;
 case 11:return detection.features.statistics.transactions;case 12:return detection.features.beam_completed;case 13:return sizeof(detection);default:return 0;}
}

/* 借助已有完整检测Golden，不用新后端为自己生成参考。 */
#include "mic16_front_context.h"
static m16_front_ring input_ring;
static m16_front_ring_storage input_ring_storage;
static mic16_spectral_workspace input_workspace;
static m16_front_context_task front_context;
static float context_pcm[40960u*16u];
static unsigned public_begin_calls;
static uint64_t front_tick;
int __real_m16_public_spectrum_begin(m16_public_spectrum_task *,const float *,unsigned,uint64_t,uint64_t);
int __wrap_m16_public_spectrum_begin(m16_public_spectrum_task *t,const float *p,unsigned n,uint64_t epoch,uint64_t frame){
 int rc=__real_m16_public_spectrum_begin(t,p,n,epoch,frame);if(!rc)public_begin_calls++;return rc;
}
int dt_setup(const float *x,const float *q,unsigned overlap){
 int rc=dt_setup_ready(x,q,overlap);return rc?rc:dt_begin(12,0,1,1000);
}
int pc_setup(const float *x,const float *q){
 int rc=dt_setup_ready(x,q,0);if(rc)return rc;
 memset(&input_ring,0,sizeof(input_ring));memset(&front_context,0,sizeof(front_context));
 mic16_spectral_init(&input_workspace);front_tick=100;public_begin_calls=0;
 rc=m16_front_ring_init(&input_ring,&input_ring_storage,&input_workspace);if(rc)return rc;
 rc=m16_front_ring_start(&input_ring,12,front_tick);if(rc)return rc;
 for(unsigned frame=0;frame<20;frame++){
  rc=m16_front_ring_begin(&input_ring,x+frame*32768u,32768u,12,frame,frame*2048u,++front_tick);if(rc)return rc;
  rc=1;while(rc==1)rc=m16_front_ring_step(&input_ring,++front_tick);if(rc)return rc;
 }
 return m16_front_context_init(&front_context,&input_ring,&detection,context_pcm,40960u*16u);
}
int pc_copy_begin(void){return m16_front_context_begin(&front_context);}
int pc_copy_step(void){return m16_front_context_step(&front_context);}
int pc_copy_finish(void){int rc=1;for(unsigned i=0;i<8000&&rc==1;i++)rc=pc_copy_step();return rc;}
int pc_begin(void){return m16_detection_begin_prepared(&detection,&front_context,quiet,8192u*16u,&detection_output,1,1000);}
int pc_retire(void){return m16_front_context_retire(&front_context);}
int pc_stop(void){return m16_front_context_stop(&front_context);}
int pc_check_copy(void){
 if(memcmp(context_pcm,pcm,sizeof(context_pcm)))return -1;
 for(unsigned f=0;f<20;f++){
  if(memcmp(detection_work.extended.physical.phat+f*51648u,input_ring_storage.spectrum[f].phat,51648u*4u))return -2;
  if(memcmp(detection_work.extended.physical.levels+f*24u,input_ring_storage.spectrum[f].zone_level_db,24u*8u))return -3;
 }return 0;
}
int pc_extra_frame(void){
 unsigned f=(unsigned)input_ring.next_frame;int rc=m16_front_ring_begin(&input_ring,pcm,32768u,12,f,f*2048u,++front_tick);
 if(rc)return rc;
 rc=1;while(rc==1)rc=m16_front_ring_step(&input_ring,++front_tick);return rc;
}
uint64_t pc_metric(unsigned k){
 switch(k){case 0:return public_begin_calls;case 1:return front_context.phase;case 2:return input_ring.reader;
 case 3:return front_context.bytes_copied;case 4:return front_context.max_bytes;case 5:return input_ring.frames_out;
 case 6:return detection.prepared_input;case 7:return detection.front_context!=0;case 8:return detection.front.spectrum.frame;
 case 9:return front_context.submitted;default:return UINT64_MAX;}
}
