#ifndef MIC16_RESAMPLE_PIPELINE_H
#define MIC16_RESAMPLE_PIPELINE_H
extern unsigned m16_resample_pipeline_enabled,m16_resample_pipeline_calls[4];
int m16_resample_pipeline_select(unsigned);
void m16_resample_pipeline_clear_tables(void);
int m16_resample_pipeline(const float *,unsigned,unsigned,unsigned,double *);
#endif
