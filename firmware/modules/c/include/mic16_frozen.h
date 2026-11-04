#ifndef MIC16_FROZEN_H
#define MIC16_FROZEN_H
#ifdef __cplusplus
extern "C" {
#endif
#define MIC16_CANDIDATES 16
#define MIC16_FRAMES 20
#define MIC16_FEATURES 130
#define MIC16_TRACKS 8
#define MIC16_POSE_KEY_BYTES 512

/* 调用方拥有缓冲区，无堆、可变全局状态或麦克风IO。权重、特征和激活为FP32，方向几何为FP64；106维与24维特征分别投影累加。 */
typedef struct {
    float h0[640], h1[640], own[1136];
    float context[215], fc64[64], fc32[32];
    double unit[48];
    unsigned char neighbors[256];
} mic16_score_workspace;

/* 输入x[c,20,130]、方位俯仰角[c,2]及0/1有效位。返回0或-1；成功时无效候选logit=-20。本函数从已提取特征开始，不从PCM开始。 */
int mic16_frozen_score(const float *x, const double *directions,
                       const unsigned char *valid, float *logits,
                       mic16_score_workspace *workspace);
double mic16_frozen_probability(float logit);
const char *mic16_frozen_checkpoint_sha256(void);

typedef struct {
    unsigned int source_id;
    double azimuth, elevation, score;
    int first_seen_cycle, last_seen_cycle, confirmed_this_cycle;
} mic16_track;

typedef struct {
    mic16_track tracks[MIC16_TRACKS];
    int count, cycle;
    unsigned int next_id;
    double last_audio_end;
    int has_previous_cycle;
    char pose_key[MIC16_POSE_KEY_BYTES];
} mic16_search_memory;

typedef struct {
    mic16_track tracks[MIC16_TRACKS], new_source;
    unsigned int expired_ids[MIC16_TRACKS];
    int count, confirmed_count, expired_count, new_source_valid;
    int cycle, pose_reset, gap_reset;
} mic16_cycle_output;

void mic16_memory_init(mic16_search_memory *memory);
/* 等价于原consume_scored_cycle与IncrementalSearchMemory.step。使用全部候选确认已知源并挑选新源；需要单调音频结束时刻和显式非空姿态/参考/来源键，长度≤511字节且不截断。contiguous=0清轨迹且本周期不收新源。返回0成功、-1输入无效、-2时间不单调；错误保留记忆。PCM连续性和参考哈希由调用方负责。 */
int mic16_consume_scored_cycle(mic16_search_memory *memory,
    const double *directions, const unsigned char *valid, const double *scores,
    double audio_end_s, const char *pose_reference_key, int contiguous,
    mic16_cycle_output *output);
#ifdef __cplusplus
}
#endif
#endif
