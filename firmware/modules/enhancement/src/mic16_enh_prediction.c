/* 从同一窗增强实现生成显式预测分支，避免复制漂移。
 * 各辅助函数为本翻译单元私有，不新增数据工作区；原音频入口仍独立保留。 */
#define M16_ENH_PREDICTION_IMPLEMENTATION 1
#include "mic16_enh_window.c"
