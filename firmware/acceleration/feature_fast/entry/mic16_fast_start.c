/* 显式优化构建在进入原main前选定精确优化；不改变原主程序源码。 */
#include "mic16_feature_fast.h"
extern int __real_main(void);
int __wrap_main(void){if(m16_feature_fast_select(3))return -1;return __real_main();}
