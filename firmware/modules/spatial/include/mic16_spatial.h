#ifndef MIC16_SPATIAL_H
#define MIC16_SPATIAL_H
#ifdef __cplusplus
extern "C" {
#endif
#define M16_SP_GRID 325
#define M16_SP_VOTES 480
#define M16_SP_MAP_FLOATS (20*24*325)
#define M16_SP_PHAT_FLOATS (20*807*32*2)
typedef struct {
    double direction[2],weight_sum;
    unsigned int seed_grid,votes,frames,bands;
} mic16_cluster;
typedef struct {
    mic16_cluster items[16];
    unsigned int count;
} mic16_pool;
typedef struct {
    float signature[32*34*2];
    double accum[20],weights[480];
    mic16_cluster clusters[325];
} mic16_spatial_workspace;
/* 调用方提供互不重叠的DDR缓冲区，不使用堆。固定几何表精确导出自冻结Python版本，不是学习参数。PHAT形状为[20,807,32,2]，空间图为[20,24,325]，俯仰优先、方位连续。写空间图前拒绝非有限值或越界PHAT。 */
int mic16_spatial_project(const float *phat,float *maps,mic16_spatial_workspace *w);
/* 未加门控的原始投票为[480,9]：时间、区域、粗频带、频率、方位、俯仰、超额能量、相干性和对比度。level为[20,24]，floor为[24]；调用方提供独立参考。 */
int mic16_spatial_votes(const float *maps,const double *level,const double *floor,
                        double *votes,unsigned short *best_indices);
int mic16_spatial_candidates(const double *votes,mic16_pool *out,mic16_spatial_workspace *w);
/* 供精确边界诊断及与旧扫描器比较使用。 */
double mic16_spatial_angle(double az,double el,double bz,double bl);
int mic16_spatial_pool_sorted(const mic16_cluster *sorted,unsigned int count,mic16_pool *out);
const float *mic16_spatial_lags(void);
int mic16_spatial_signature(unsigned int grid,unsigned int pair,unsigned int bin,float *ri);
#ifdef __cplusplus
}
#endif
#endif
