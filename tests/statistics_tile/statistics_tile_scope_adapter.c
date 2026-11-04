/* 沿用原完整检测测试入口和数值门，仅将统计实现接到新分块驱动。 */
#include "mic16_statistics_neon.h"
#include "mic16_statistics_tile_batch.h"
extern int m16_tile_host_bind(void);
extern int __real_mic16_statistics18_pair_cache(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
unsigned m16_statistics_neon_mode,m16_statistics_neon_calls[4];
int m16_statistics_neon_select(unsigned mode){
 unsigned i;if(mode>1)return -1;
 if(mode && m16_tile_host_bind())return -2;
 m16_statistics_neon_mode=mode;for(i=0;i<4;i++)m16_statistics_neon_calls[i]=0;return 0;
}
int m16_statistics_neon(const double *d,unsigned n,float *out,mic16_extended_workspace *w,mic16_feature_reuse_report *r){
 int rc;
 /* 独立尾候选测试也可以直接进入此入口。 */
 if(!m16_statistics_neon_mode && m16_tile_host_bind())return -2;
 rc=m16_tile_batch_compute(d,n,out,w,r);
 m16_statistics_neon_calls[0]=m16_tile_batch_counts.calls;
 m16_statistics_neon_calls[1]=m16_tile_batch_counts.rows;
 m16_statistics_neon_calls[2]=m16_tile_batch_counts.rotation_groups;
 return rc;
}
int __wrap_mic16_statistics18_pair_cache(const double *d,unsigned n,float *out,mic16_extended_workspace *w,mic16_feature_reuse_report *r){
 return m16_statistics_neon_mode?m16_statistics_neon(d,n,out,w,r):__real_mic16_statistics18_pair_cache(d,n,out,w,r);
}
