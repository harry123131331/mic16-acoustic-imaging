#ifndef MIC16_COOPERATE_H
#define MIC16_COOPERATE_H
/* 只在普通主线程的安全检查点调用，不得在中断或lwIP回调中注册/执行。
 * 默认展开为空表达式；历史构建不改变算法和调度。 */
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
int m16_cooperate_bind(void (*service)(void *),void *context);
void m16_cooperate(void);
#define M16_COOPERATE() m16_cooperate()
#else
#define M16_COOPERATE() ((void)0)
#endif
#endif
