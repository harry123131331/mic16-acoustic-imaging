/* 新计算图沿用数值后端的诊断计数，不引入旧同步dispatch入口。
 * 计数仅用于诊断，不参与候选、特征、评分或音频计算。 */
unsigned m16_feature_fast_calls[3];
unsigned m16_fft_fast_calls[3];
