#include "mic16_dataflow_fast.h"
unsigned m16_dataflow_mode,m16_dataflow_calls[6];
int m16_dataflow_select(unsigned m){unsigned i;if(m>2)return -1;m16_dataflow_mode=m;for(i=0;i<6;i++)m16_dataflow_calls[i]=0;return 0;}
#ifdef M16_DATAFLOW_ARM_WRAP
#define SCAN __wrap_m16_scanner_refine_accumulators
#define OLD_SCAN __real_m16_scanner_refine_accumulators
#define RESAMPLE __wrap_mic16_resample_channel
#define OLD_RESAMPLE __real_mic16_resample_channel
#else
#define SCAN m16_scanner_refine_accumulators
#define OLD_SCAN m16_original_refine_accumulators
#define RESAMPLE mic16_resample_channel
#define OLD_RESAMPLE m16_original_resample_channel
#endif
extern int OLD_SCAN(const float *,float *,mic16_spatial_workspace *,m16_scanner_refinement *);
extern int OLD_RESAMPLE(const float *,unsigned,unsigned,unsigned,double *);
int SCAN(const float *x,float *m,mic16_spatial_workspace *w,m16_scanner_refinement *r){
 return m16_dataflow_mode?m16_scanner_refine_dataflow(x,m,w,r):OLD_SCAN(x,m,w,r);
}
int RESAMPLE(const float *x,unsigned n,unsigned s,unsigned c,double *o){
 return m16_dataflow_mode>=2?m16_resample_dataflow(x,n,s,c,o):OLD_RESAMPLE(x,n,s,c,o);
}
