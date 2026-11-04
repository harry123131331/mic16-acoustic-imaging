#include "mic16_statistics_tile_runtime.h"
#include <stdio.h>
#include <string.h>
extern int m16_tile_host_reset(unsigned);
extern m16_tile_runtime *m16_tile_host_state(void);
extern unsigned m16_tile_host_flush,m16_tile_host_flush_obs,m16_tile_host_invalidates,m16_tile_host_cache_errors,m16_tile_host_starts;
static unsigned checks,errors;
static void check(int ok,const char *name){checks++;if(!ok){errors++;printf("RUNTIME_FAIL %s\n",name);}}
/* 此单元只测驱动；完整分块绑定由完整检测夹具另行核验。 */
int m16_tile_batch_bind(const m16_tile_batch_backend *b){return b && b->arena?0:-1;}
int main(void){
 m16_tile_runtime *w;unsigned mode,bytes,i;int expected[6]={0,M16_TILE_TIMEOUT,M16_TILE_RETURN,M16_TILE_GUARD,M16_TILE_GUARD,M16_TILE_GUARD};
 check(!m16_tile_host_reset(0),"初始化");w=m16_tile_host_state();
 check(m16_tile_runtime_available(w),"初始所有权");
 check(m16_tile_runtime_submit(w,0,8,4,1)==M16_TILE_INVALID && !m16_tile_host_starts,"拒绝空帧且不启动");
 check(!m16_tile_runtime_submit(w,4,8,4,1),"非阻塞提交");
 check(!m16_tile_runtime_available(w),"进行中不能复用");
 check(m16_tile_runtime_submit(w,4,8,4,1)==M16_TILE_INVALID,"拒绝重复提交");
 check(!m16_tile_runtime_poll(w),"完成取回");
 check(m16_tile_runtime_available(w),"成功归还所有权");
 check(w->report.batches==1 && !w->report.quarantined,"完成报告");
 check(m16_tile_host_flush_obs==8192 && m16_tile_host_flush==18560,"首次精确清洁范围");
 check(m16_tile_host_invalidates==2176 && !m16_tile_host_cache_errors,"输出整线失效");
 bytes=m16_tile_host_flush;
 check(!m16_tile_runtime_execute(w,3,8,1,0),"尾帧尾候选观测复用");
 check(m16_tile_host_flush-bytes==10368 && m16_tile_host_flush_obs==8192,"复用跳过观测清洁");
 check(!m16_tile_runtime_execute(w,1,1,1,1),"观测变化重新清洁");
 check(m16_tile_host_flush_obs==16384 && !m16_tile_host_cache_errors,"变更清洁计数");
 for(mode=1;mode<=5;mode++){
  check(!m16_tile_host_reset(mode),"异常前初始化");w=m16_tile_host_state();
  check(m16_tile_runtime_execute(w,1,1,1,1)==expected[mode],"异常返回");
  check(w->quarantined && !m16_tile_runtime_available(w),"异常隔离");
  bytes=m16_tile_host_flush;
  check(m16_tile_runtime_execute(w,1,1,1,1)==M16_TILE_INVALID && m16_tile_host_flush==bytes,"隔离不触碰缓存");
  check(m16_tile_runtime_init(w,&w->io,M16_TILE_BUILD_ABI)==M16_TILE_INVALID,"隔离不能自行初始化");
 }
 check(!m16_tile_host_reset(6),"done与idle分离初始化");w=m16_tile_host_state();
 check(!m16_tile_runtime_submit(w,1,1,1,1),"分离完成提交");
 check(m16_tile_runtime_poll(w)==1 && !w->report.batches && !m16_tile_runtime_available(w),"未idle不能报完成");
 check(!m16_tile_runtime_poll(w),"读清done后等待idle");
 check(!m16_tile_host_reset(0),"时钟测试初始化");w=m16_tile_host_state();
 check(!m16_tile_runtime_submit(w,1,1,1,1),"时钟测试提交");w->last_tick+=100;
 check(m16_tile_runtime_poll(w)==M16_TILE_CLOCK && w->quarantined,"时钟倒退隔离");
 check(m16_tile_host_reset(8)==M16_TILE_ABI,"寄存器不兼容");
 check(!m16_tile_host_reset(0),"ABI授权初始化");w=m16_tile_host_state();
 {m16_scanner_io io=w->io;check(m16_tile_runtime_init(w,&io,0)==M16_TILE_ABI,"拒绝未配套版本");}
 /* 所有4×8×4尾维度，共128种；无效输出槽必须保持。 */
 for(i=0;i<128;i++){
  unsigned f=i/32+1,b=(i/4)%8+1,c=i%4+1;
  check(!m16_tile_host_reset(0),"尾维度初始化");w=m16_tile_host_state();
  check(!m16_tile_runtime_execute(w,f,b,c,1),"尾维度边界");
 }
 printf("TILE_RUNTIME_PASS checks=%u errors=%u HOST_REGISTER_MODEL_NOT_BOARD\n",checks,errors);
 return errors?1:0;
}
