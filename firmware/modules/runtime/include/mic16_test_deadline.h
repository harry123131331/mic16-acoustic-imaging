#ifndef MIC16_TEST_DEADLINE_H
#define MIC16_TEST_DEADLINE_H
#include "mic16_resident_control.h"
enum {M16_TD_NONE=0,M16_TD_DEADLINE=1,M16_TD_MANUAL_STOP=2,M16_TD_CLOCK=3};
enum {M16_TD_INVALID=-2400,M16_TD_COUNTER=-2401};
typedef struct {
 unsigned magic,armed,epoch,reason;
 uint64_t limit_ticks,finish_margin_ticks,started_tick,last_tick,requested_tick;
 uint64_t automatic_stops,manual_stops,late_ticks;
 m16_resident_control *control;
} m16_test_deadline;
#ifdef __cplusplus
extern "C" {
#endif
/* 只绑定到同一PS主循环的控制对象。limit=0为禁用，不改变人工持续模式。 */
int m16_test_deadline_init(m16_test_deadline *,m16_resident_control *,uint64_t limit,uint64_t finish_margin);
/* 必须在常驻任务领取action前调用。只登记已有STOP动作，不直接复位DMA或清队列。 */
int m16_test_deadline_step(m16_test_deadline *,uint64_t now);
#ifdef __cplusplus
}
#endif
#endif
