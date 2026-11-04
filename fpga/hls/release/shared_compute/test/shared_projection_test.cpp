/* 三种数值语义的混合批次，包括FP32非正规输入对应的正常FP64乘积。 */
#include "shared_projection_engine.h"
#include "mic16_projection_protocol.h"
#include <stdio.h>
/* 联仿包装器读取完整接口depth；后备数组必须足额，合法工作范围仍只有4万字。 */
static const unsigned PORT_DEPTH=524288,VALID_WORDS=40000;
static uint64_t arena[PORT_DEPTH];
static const uint64_t GUARD=UINT64_C(0xa73b9d64c8e21f05);
static uint64_t fp(float real,float imag){union{float f;uint32_t u;}a,b;a.f=real;b.f=imag;return a.u|((uint64_t)b.u<<32);}
static uint64_t dp(double value){union{double d;uint64_t u;}r;r.d=value;return r.u;}
static unsigned cursor=64;
static unsigned add(unsigned index,unsigned mode,unsigned frames,unsigned bins,unsigned count,bool tiny){
    unsigned no=frames*bins*32;
    unsigned nc=mode==1?((count+3)/4)*bins*32*4:count*bins*32;
    unsigned nr=mode==1?frames*bins*count*2:frames*count;
    unsigned obs=cursor;cursor+=no+8;unsigned coef=cursor;cursor+=nc+8;
    unsigned out=cursor;cursor+=nr+8;unsigned report=cursor;cursor+=2;
    if(cursor>VALID_WORDS)return 0;
    for(unsigned i=0;i<no;i++)arena[obs+i]=tiny?1:fp(.5f,.25f);
    for(unsigned i=0;i<nc;i++)arena[coef+i]=tiny?1:fp(.75f,-.125f);
    arena[index*8]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|((uint64_t)mode<<48);
    arena[index*8+1]=1001+index;
    arena[index*8+2]=obs|((uint64_t)coef<<32);arena[index*8+3]=out|((uint64_t)report<<32);
    arena[index*8+4]=frames|((uint64_t)bins<<16)|((uint64_t)count<<32);
    arena[index*8+5]=nr;arena[index*8+6]=no|((uint64_t)nc<<32);arena[index*8+7]=0;
    return 1;
}
int main(){
    for(unsigned i=0;i<PORT_DEPTH;i++)arena[i]=GUARD;
    if(!add(0,1,4,9,5,false)||!add(1,2,4,34,3,false)||!add(2,3,3,34,16,false)||!add(3,3,1,1,1,true))return 1;
    m16_projection_job plan[8];
    if(m16_projection_validate(arena,VALID_WORDS,0,4,plan))return 2;
    if(m16_shared_projection_engine(arena,VALID_WORDS,0,4)!=4)return 3;
    for(unsigned j=0;j<4;j++){
        const m16_projection_job &p=plan[j];
        if(arena[p.report]!=p.tag || arena[p.report+1]!=((uint64_t)p.nr<<32))return 4;
        uint64_t expected=j==0?fp(5.5f,4.f):j==1?fp(374.f,0.f):j==2?dp(374.):((uint64_t)(1023-293)<<52);
        for(unsigned i=0;i<p.nr;i++)if(arena[p.out+i]!=expected){
            printf("三模式结果不一致 job=%u result=%u actual=%llx expected=%llx\n",j,i,
                (unsigned long long)arena[p.out+i],(unsigned long long)expected);return 5;
        }
        if(arena[p.out+p.nr]!=GUARD || arena[p.obs+p.no]!=GUARD || arena[p.coef+p.nc]!=GUARD)return 6;
    }
    for(unsigned i=cursor;i<PORT_DEPTH;i++)if(arena[i]!=GUARD)return 7;
    puts("M16_SHARED_THREE_MODES_AND_FP32_SUBNORMAL_PRODUCT_PASS_NOT_FULL_PIPELINE");
    return 0;
}
/* APCC前端按同一实际C验证器编译，ARM生产版本仍使用C11单独编译。 */
#include "../../../../firmware/acceleration/projection_engine/src/mic16_projection_validate.c"
