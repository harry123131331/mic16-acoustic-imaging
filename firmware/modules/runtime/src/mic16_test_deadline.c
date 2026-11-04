#include "mic16_test_deadline.h"
#include <string.h>
#define MAGIC 0x54444c31u
int m16_test_deadline_init(m16_test_deadline *t,m16_resident_control *c,uint64_t limit,uint64_t margin){
 if(!t||t->magic||!c||(limit&&margin>=limit)||(!limit&&margin))return M16_TD_INVALID;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->control=c;t->limit_ticks=limit;t->finish_margin_ticks=margin;return 0;
}
int m16_test_deadline_step(m16_test_deadline *t,uint64_t now){
 m16_resident_control *c;uint64_t elapsed,request_at;unsigned backwards;
 if(!t||t->magic!=MAGIC||!t->control)return M16_TD_INVALID;
 if(!t->limit_ticks)return 0;
 c=t->control;
 if(c->epoch!=t->epoch){
  /* 只为本主循环尚未领取的真实START建立截止期，接入一个已运行会话不补造起点。 */
  if(c->state==M16_CTL_STARTING&&c->action==M16_CTL_START){
   t->epoch=c->epoch;t->armed=1;t->reason=M16_TD_NONE;t->started_tick=t->last_tick=now;
   t->requested_tick=t->late_ticks=0;
  }
  return 0;
 }
 if(!t->armed)return 0;
 if(c->state!=M16_CTL_STARTING&&c->state!=M16_CTL_RUNNING){
  if(c->state==M16_CTL_DRAINING){t->reason=M16_TD_MANUAL_STOP;t->manual_stops++;}
  t->armed=0;return 0;
 }
 backwards=now<t->last_tick;t->last_tick=now;
 elapsed=backwards?0:now-t->started_tick;request_at=t->limit_ticks-t->finish_margin_ticks;
 if(!backwards&&elapsed<request_at)return 0;
 if(c->action==M16_CTL_START)return 0;
 if(c->action)return M16_TD_INVALID;
 if(c->stops==UINT64_MAX)return M16_TD_COUNTER;
 /* 本组件与m16_control_handle、常驻任务共享唯一前台所有者。
  * 内部截止期也是正常停止事件；保留外部nonce、请求编号和幂等缓存，不伪造UDP命令。 */
 c->state=M16_CTL_DRAINING;c->action=M16_CTL_STOP;c->stops++;
 t->reason=backwards?M16_TD_CLOCK:M16_TD_DEADLINE;t->armed=0;t->requested_tick=now;t->automatic_stops++;
 t->late_ticks=!backwards&&elapsed>request_at?elapsed-request_at:0;return 1;
}
