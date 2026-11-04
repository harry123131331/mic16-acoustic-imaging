/* 由C编译器读取冻结的十六进制FP32几何表，供HLS C++测试台按C接口调用。 */
#include "mic16_spatial_tables.h"
const float *mic16_spatial_lags(void){return &m16_grid_lags[0][0];}
