#ifndef MIC16_SHARED_SPECTRUM_H
#define MIC16_SHARED_SPECTRUM_H
#include "mic16_tf_phat_frontend_hls_q2_24.h"
/* mode=0：旧整数前端；mode=1：16列、每列2048点预加窗F32，两个样本/64位字。
 * 候选模式不再去均值或乘窗。输出同旧频谱布局，通道×807，实低32/虚高32。
 * 模式1仅处理active_channels列；非法参数返回-1且不写内存。输入有限值由PS验证。
 * 本原型替换前端工作模式，不新增第二套FFT；尚未替换正式IP。 */
/* mode=4：候选连续布局，每变换2048个F32，两个样本/word；输出格式不变。 */
int m16_sp_shared(ap_uint<64> *,unsigned,unsigned,unsigned,unsigned,unsigned);
#endif
