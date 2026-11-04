#ifndef MIC16_WINDOW_CAPTURE_ARM_H
#define MIC16_WINDOW_CAPTURE_ARM_H
#include "mic16_window_capture.h"
typedef struct {
 m16_window_capture *runtime;
 unsigned attached;
} m16_window_capture_arm;
/* 沿用network已初始化的GIC，不初始化/清空整个GIC或其它中断。
 * 三个缓冲采用当前裸机DDR恒等映射；只准使用匹配整窗DMA BD的候选包。
 * attach不开始采音，须先分步准备缓冲，再由常驻START调用capture_start。 */
int m16_window_capture_arm_attach(m16_window_capture_arm *,m16_window_capture *,const m16_window_capture_config *);
int m16_window_capture_arm_detach(m16_window_capture_arm *);
void m16_window_capture_arm_handler(void *);
#endif
