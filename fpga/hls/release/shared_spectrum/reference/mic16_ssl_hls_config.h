#ifndef MIC16_SSL_HLS_CONFIG_H
#define MIC16_SSL_HLS_CONFIG_H

#include "geometry_convention.h"

#define MIC16_HLS_CHANNELS              16
#define MIC16_HLS_FRAME_SAMPLES         2048
#define MIC16_HLS_FFT_LEN               4096
#define MIC16_HLS_PAIR_COUNT            32
#define MIC16_HLS_MAX_LAG               20
#define MIC16_HLS_LAG_COUNT             (2 * MIC16_HLS_MAX_LAG + 1)
#define MIC16_HLS_GCC_FEATURE_DIM       (MIC16_HLS_PAIR_COUNT * MIC16_HLS_LAG_COUNT)
#define MIC16_HLS_ANGLE_COUNT           72
#define MIC16_HLS_RANGE_COUNT           5
#define MIC16_HLS_GLOBAL_QUALITY_DIM    6
#define MIC16_HLS_GQ_MEAN_RELIABILITY  0
#define MIC16_HLS_GQ_STD_RELIABILITY   1
#define MIC16_HLS_GQ_MAX_SRP_PEAK      2
#define MIC16_HLS_GQ_SRP_PEAK_MARGIN   3
#define MIC16_HLS_GQ_SRP_ENTROPY       4
#define MIC16_HLS_GQ_VALID_PAIR_RATIO  5
#define MIC16_HLS_FRAME_FEATURE_DIM     (MIC16_HLS_ANGLE_COUNT + MIC16_HLS_PAIR_COUNT + MIC16_HLS_GLOBAL_QUALITY_DIM)
#define MIC16_HLS_TEMPORAL_WINDOW       5
#define MIC16_HLS_NET_INPUT_DIM         (MIC16_HLS_TEMPORAL_WINDOW * MIC16_HLS_FRAME_FEATURE_DIM)

#ifndef MIC16_HLS_LOW_RESOURCE_GCC
#define MIC16_HLS_LOW_RESOURCE_GCC      0
#endif
#ifndef MIC16_HLS_SOURCE_REUSE_GCC
#define MIC16_HLS_SOURCE_REUSE_GCC      0
#endif
#if MIC16_HLS_LOW_RESOURCE_GCC && MIC16_HLS_SOURCE_REUSE_GCC
#error "Select only one GCC spectrum scheduling mode"
#endif
#ifndef MIC16_HLS_LOW_RESOURCE_DAS
#define MIC16_HLS_LOW_RESOURCE_DAS      0
#endif
#define MIC16_HLS_NET_HIDDEN_DIM        128
#define MIC16_HLS_CCTCN_CHANNELS        40
#define MIC16_HLS_CCTCN_DIR_KERNEL      5
#define MIC16_HLS_CCTCN_TCN_KERNEL      3
#define MIC16_HLS_PACKET_SAMPLES_PER_CHANNEL 16
#define MIC16_HLS_PACKET_WORDS          (MIC16_HLS_PACKET_SAMPLES_PER_CHANNEL * MIC16_HLS_CHANNELS)

#define MIC16_SSL_PARAM_MAGIC           0x4D313653u
#define MIC16_SSL_PARAM_VERSION         0x00010002u
#define MIC16_SSL_ALGORITHM_VERSION     0x4354434Eu
#define MIC16_SSL_PARAM_FLAG_DEFAULT_SIM 0x00000001u
#define MIC16_SSL_PARAM_FLAG_REAL_CALIB  0x00000002u
#define MIC16_SSL_PARAM_BLOB_BYTES       465452u
#define MIC16_SSL_RESULT_BYTES           64u

#define MIC16_SSL_QUALITY_FLAG_LOW_CONF         0x00000001u
#define MIC16_SSL_QUALITY_FLAG_SEQUENCE_WARMUP  0x00000002u
#define MIC16_SSL_QUALITY_FLAG_INPUT_FRAME      0x00000004u
#define MIC16_SSL_QUALITY_FLAG_PARAM_INVALID    0x00000008u
#define MIC16_SSL_QUALITY_FLAG_LOW_ENERGY       0x00000010u
#define MIC16_SSL_QUALITY_FLAG_SELFTEST         0x00000020u
#define MIC16_SSL_QUALITY_REJECT_MASK           0x0000001Fu

typedef int mic16_sample_t;

typedef struct {
    float re;
    float im;
} mic16_complex_t;

typedef struct {
    unsigned int magic;
    unsigned int version;
    unsigned int total_bytes;
    unsigned int flags;
    unsigned int crc32;
    unsigned int payload_hash;

    float channel_delay_offset[MIC16_HLS_CHANNELS];
    float channel_health[MIC16_HLS_CHANNELS];
    float pair_health[MIC16_HLS_PAIR_COUNT];
    unsigned int pair_enable_mask[MIC16_HLS_PAIR_COUNT];
    float array_azimuth_offset_deg;
    float srp_lag_table[MIC16_HLS_ANGLE_COUNT][MIC16_HLS_RANGE_COUNT][MIC16_HLS_PAIR_COUNT];

    float quality_alpha_psr;
    float quality_alpha_sharpness;
    float quality_alpha_health;
    float reliability_min;
    float reliability_max;
    float quality_eps;
    float frame_energy_min;
    unsigned int valid_freq_bin_min;
    unsigned int valid_freq_bin_max;
    float phat_mag_floor;

    float input_mean[MIC16_HLS_NET_INPUT_DIM];
    float input_std[MIC16_HLS_NET_INPUT_DIM];
    float temperature;
    float confidence_threshold;
    float valid_pair_threshold;
    unsigned int min_valid_pairs;

    float fc1_weight[MIC16_HLS_NET_HIDDEN_DIM][MIC16_HLS_NET_INPUT_DIM];
    float fc1_bias[MIC16_HLS_NET_HIDDEN_DIM];
    float angle_weight[MIC16_HLS_ANGLE_COUNT][MIC16_HLS_NET_HIDDEN_DIM];
    float angle_bias[MIC16_HLS_ANGLE_COUNT];
    float confidence_weight[MIC16_HLS_NET_HIDDEN_DIM];
    float confidence_bias;

    float steering_delay_table[MIC16_HLS_ANGLE_COUNT][MIC16_HLS_CHANNELS];
    float beamformer_channel_weight[MIC16_HLS_CHANNELS];

    float cctcn_direction_conv1_weight[MIC16_HLS_CCTCN_CHANNELS][1][MIC16_HLS_CCTCN_DIR_KERNEL];
    float cctcn_direction_conv1_bias[MIC16_HLS_CCTCN_CHANNELS];
    float cctcn_direction_conv2_weight[MIC16_HLS_CCTCN_CHANNELS][MIC16_HLS_CCTCN_CHANNELS][MIC16_HLS_CCTCN_DIR_KERNEL];
    float cctcn_direction_conv2_bias[MIC16_HLS_CCTCN_CHANNELS];
    float cctcn_quality_weight[MIC16_HLS_CCTCN_CHANNELS][MIC16_HLS_PAIR_COUNT + MIC16_HLS_GLOBAL_QUALITY_DIM];
    float cctcn_quality_bias[MIC16_HLS_CCTCN_CHANNELS];
    float cctcn_tcn1_weight[MIC16_HLS_CCTCN_CHANNELS][MIC16_HLS_CCTCN_CHANNELS][MIC16_HLS_CCTCN_TCN_KERNEL];
    float cctcn_tcn1_bias[MIC16_HLS_CCTCN_CHANNELS];
    float cctcn_tcn2_weight[MIC16_HLS_CCTCN_CHANNELS][MIC16_HLS_CCTCN_CHANNELS][MIC16_HLS_CCTCN_TCN_KERNEL];
    float cctcn_tcn2_bias[MIC16_HLS_CCTCN_CHANNELS];
    float cctcn_angle_weight[MIC16_HLS_ANGLE_COUNT][MIC16_HLS_CCTCN_CHANNELS];
    float cctcn_angle_bias[MIC16_HLS_ANGLE_COUNT];
    float cctcn_confidence_weight[MIC16_HLS_CCTCN_CHANNELS];
    float cctcn_confidence_bias;
} mic16_ssl_param_blob_t;

typedef struct {
    unsigned int algorithm_version;
    unsigned int predicted_class;
    float predicted_angle_deg;
    unsigned int top3_classes[3];
    float top3_logits[3];
    float confidence_head_raw;
    float softmax_confidence;
    float entropy;
    float peak_margin;
    unsigned int valid_pair_count;
    unsigned int quality_flag;
    unsigned int latency_cycles;
} mic16_ssl_result_t;

typedef struct {
    float frame_energy;
    unsigned int frame_valid;
    unsigned int low_energy_flag;
    unsigned int valid_freq_bin_min;
    unsigned int valid_freq_bin_max;
    float phat_mag_floor;
} mic16_gcc_status_t;

typedef struct {
    float peak_value;
    float second_peak_value;
    float psr;
    float peak_sharpness;
    float peak_width;
    float sidelobe_std;
    float pair_energy;
    unsigned int valid_flag;
} mic16_pair_quality_t;

/* Keep the Python exporter, PS loader, and HLS kernel on one binary ABI. */
typedef char mic16_ssl_param_blob_size_check[
    (sizeof(mic16_ssl_param_blob_t) == MIC16_SSL_PARAM_BLOB_BYTES) ? 1 : -1];
typedef char mic16_ssl_result_size_check[
    (sizeof(mic16_ssl_result_t) == MIC16_SSL_RESULT_BYTES) ? 1 : -1];

#endif
