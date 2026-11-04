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

/*
 * Portable-C D3b reference kernel.
 *
 * observation_bin_pair is bin-major: [bin_count][pair_count].
 * initial_phasor, bin_rotator and phasor_scratch are grid-major:
 * [grid_count][pair_count].  output_zone_grid is [zone_count][grid_count].
 * zone_offsets contains zone_count + 1 contiguous offsets and must begin at
 * zero and end at bin_count.
 *
 * The caller owns all storage.  No heap, OS service, static mutable state or
 * full [grid][pair][bin] steering table is used.
 */
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
