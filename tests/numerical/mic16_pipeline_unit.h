#ifndef MIC16_PIPELINE_UNIT_H
#define MIC16_PIPELINE_UNIT_H
#include "mic16_pipeline.h"
typedef struct {
    unsigned int checks, errors, completed_cases, current_case;
    unsigned int pcm_checksum, reference_checksum, qualified;
    double feature_max_error, logit_max_error, direction_max_error, score_max_error;
} mic16_pipeline_test_report;
extern volatile mic16_pipeline_test_report g_mic16_pipeline_test;
void mic16_pipeline_test_prepare(unsigned int which);
int mic16_pipeline_test_compute(void);
int mic16_pipeline_test_check(unsigned int which);
unsigned int mic16_pipeline_test_static_bytes(void);
#endif
