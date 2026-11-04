/* 同一双RAM设备模型依次执行实际空间模式3和统计模式1。 */
#include "mic16_projection_runtime.h"
#include "shared_projection_engine.h"
#include <string.h>
extern "C" void *fp_runtime(void);
extern "C" uint64_t pp_get(unsigned,unsigned);
extern "C" void pp_word(unsigned,uint64_t,unsigned);
extern "C" void pp_control(unsigned);
static uint64_t memory[40000],before[40000];
static unsigned char allowed[40000];
static unsigned read_only_errors;
extern "C" int fp_hardware(unsigned kind){
 m16_projection_runtime *r=(m16_projection_runtime *)fp_runtime();
 if(!r || r->phase!=M16_PR_DEVICE || r->words!=40000u || (r->jobs!=1u && r->jobs!=7u) ||
  r->io.read(r->io.context,0)!=0 || r->io.read(r->io.context,0x18)!=r->io.arena_address ||
  r->io.read(r->io.context,0x24)!=40000u || r->io.read(r->io.context,0x2c)!=8u || r->io.read(r->io.context,0x34)!=r->jobs)return -999;
 for(unsigned i=0;i<40000u;i++)before[i]=memory[i]=pp_get(i,1);
 memset(allowed,0,sizeof(allowed));
 for(unsigned j=0;j<r->jobs;j++){const m16_projection_job &p=r->plan[j];memset(allowed+p.out,1,p.nr);memset(allowed+p.report,1,2);}
 int rc=m16_shared_projection_engine(memory,40000,8,r->jobs);
 for(unsigned i=0;i<40000u;i++)if(!allowed[i] && memory[i]!=before[i])read_only_errors++;
 if(kind==1)memory[0]^=1u;
 if(kind==2)memory[r->plan[0].report]^=1u;
 if(kind==3)memory[r->plan[0].out]=UINT64_C(0x3f0000007fc00000);
 for(unsigned i=0;i<40000u;i++)if(memory[i]!=before[i])pp_word(i,memory[i],1);
 r->io.write(r->io.context,0x10,(uint32_t)rc);pp_control(6);return rc;
}
extern "C" unsigned fp_read_only_errors(void){return read_only_errors;}
