/* 通过已有双RAM驱动模型执行实际共享核；计数与人为延迟都不是实板周期。 */
#include "mic16_projection_runtime.h"
#include "shared_projection_engine.h"
#include <string.h>
extern "C" void *pp_runtime_bridge(unsigned);
extern "C" uint64_t pp_get(unsigned,unsigned);
extern "C" void pp_word(unsigned,uint64_t,unsigned);
extern "C" void pp_control(unsigned);
static m16_projection_runtime *runtime;
static uint64_t memory[40000],before[40000];
static unsigned char allowed[40000];
static unsigned read_only_errors;
extern "C" void *sp_runtime_bridge(void){
 runtime=(m16_projection_runtime *)pp_runtime_bridge(0x02000000u);read_only_errors=0;return runtime;
}
extern "C" int sp_hardware(unsigned kind){
 if(!runtime || runtime->phase!=M16_PR_DEVICE || runtime->words!=40000u || runtime->jobs!=1u ||
  runtime->io.read(runtime->io.context,0)!=0 || runtime->io.read(runtime->io.context,0x18)!=runtime->io.arena_address ||
  runtime->io.read(runtime->io.context,0x24)!=40000u || runtime->io.read(runtime->io.context,0x2c)!=8u ||
  runtime->io.read(runtime->io.context,0x34)!=1u)return -999;
 for(unsigned i=0;i<40000u;i++)before[i]=memory[i]=pp_get(i,1);
 memset(allowed,0,sizeof(allowed));
 for(unsigned j=0;j<runtime->jobs;j++){
  const m16_projection_job &p=runtime->plan[j];memset(allowed+p.out,1,p.nr);memset(allowed+p.report,1,2);
 }
 int rc=m16_shared_projection_engine(memory,40000,8,1);
 for(unsigned i=0;i<40000u;i++)if(!allowed[i] && memory[i]!=before[i])read_only_errors++;
 if(kind==1)memory[0]^=1u;
 if(kind==2)memory[runtime->plan[0].report]^=1u;
 if(kind==3)memory[runtime->plan[0].out]=UINT64_C(0x3f0000007fc00000);
 for(unsigned i=0;i<40000u;i++)if(memory[i]!=before[i])pp_word(i,memory[i],1);
 runtime->io.write(runtime->io.context,0x10,(uint32_t)rc);pp_control(6);return rc;
}
extern "C" unsigned sp_read_only_errors(void){return read_only_errors;}
