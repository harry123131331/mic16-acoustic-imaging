#ifndef MIC16_FAST_SCAN_GRID_H
#define MIC16_FAST_SCAN_GRID_H
#ifdef __cplusplus
extern "C" {
#endif
/* 候选专用：板面方位0/15/.../360，俯仰0/7.5/.../90；25列13行。
 * 0/360缝及法线为同一物理方向，邻域按三维夹角处理；不改冻结C16几何。 */
const float *m16_fast_scan_lags(void);
const double *m16_fast_scan_raw_angles(unsigned grid);
int m16_fast_scan_near(unsigned a,unsigned b);
#ifdef __cplusplus
}
#endif
#endif
