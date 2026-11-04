/* 用C99直接编译原始十六进制浮点表，独立核验半表ROM的每一位。 */
#include "../../../../firmware/modules/extended/include/mic16_resample_tables.h"
double m16_fir_reference_coefficient(unsigned index){return m16_resample_h[index];}
