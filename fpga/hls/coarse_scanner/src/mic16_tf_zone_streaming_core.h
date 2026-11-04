#ifndef MIC16_TF_ZONE_STREAMING_CORE_H
#define MIC16_TF_ZONE_STREAMING_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define MIC16_TF_ZONE_STREAM_OK             0
#define MIC16_TF_ZONE_STREAM_ERR_ARGUMENT  -1
#define MIC16_TF_ZONE_STREAM_ERR_DIMENSION -2
#define MIC16_TF_ZONE_STREAM_ERR_PARTITION -3

#define MIC16_TF_ZONE_STREAM_MAX_GRID   325U
#define MIC16_TF_ZONE_STREAM_MAX_PAIRS   32U
#define MIC16_TF_ZONE_STREAM_MAX_BINS    807U
#define MIC16_TF_ZONE_STREAM_MAX_ZONES    24U

typedef struct {
    float re;
    float im;
} mic16_complex_f32_t;

/* 可移植单精度C参考接口：观测按[频点,通道对]，相位按[方向,通道对]。
 * 分区偏移首项0、末项频点总数，缓存由调用者独占，不申请堆或OS资源。 */
int mic16_tf_zone_streaming_scores(
    const mic16_complex_f32_t *observation_bin_pair,
    const mic16_complex_f32_t *initial_phasor,
    const mic16_complex_f32_t *bin_rotator,
    unsigned int grid_count,
    unsigned int pair_count,
    unsigned int bin_count,
    const unsigned short *zone_offsets,
    unsigned int zone_count,
    mic16_complex_f32_t *phasor_scratch,
    float *output_zone_grid);

#ifdef __cplusplus
}
#endif

#endif
