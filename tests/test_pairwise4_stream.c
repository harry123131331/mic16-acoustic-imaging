/* 比较每个二叉树节点及浮点原字节，并单独报告宿主核耗时；不作ARM提速结论。 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <windows.h>
typedef float v4 __attribute__((vector_size(16)));
typedef uint32_t u4 __attribute__((vector_size(16)));
static inline v4 clean4(v4 f){union{v4 f;u4 u;}v;u4 mask;v.f=f;mask=(u4)((v.u&(u4){0x7f800000u,0x7f800000u,0x7f800000u,0x7f800000u})==(u4){0,0,0,0});v.u=(v.u&~mask)|(v.u&(u4){0x80000000u,0x80000000u,0x80000000u,0x80000000u}&mask);return v.f;}
static inline v4 add4(v4 a,v4 b){return clean4(a+b);}
static inline v4 dup4(float f){return clean4((v4){f,f,f,f});}
#include "mic16_pairwise4_stream.h"
static v4 input[1088];static unsigned checks,errors;static volatile float checksum;static volatile unsigned actual_calls[3];
static __attribute__((noinline)) v4 original_sum(unsigned n){
    v4 values[1088];unsigned i;actual_calls[0]++;
    for(i=0;i<n;i++)values[i]=input[i];
    while(n>1){unsigned pairs=n/2;for(i=0;i<pairs;i++)values[i]=add4(values[2*i],values[2*i+1]);if(n&1u)values[pairs]=values[n-1];n=pairs+(n&1u);}
    return values[0];
}
static __attribute__((noinline)) v4 stream_sum(unsigned n){
    m16_pairwise4_stream s;unsigned i;actual_calls[1]++;m16_pairwise4_init(&s);
    for(i=0;i<n;i++)m16_pairwise4_push(&s,input[i]);
    return m16_pairwise4_finish(&s);
}
static __attribute__((noinline)) v4 block_sum(unsigned n){
    m16_pairwise4_block_stream s;unsigned i;actual_calls[2]++;m16_pairwise4_block_init(&s);
    for(i=0;i<n;i++)m16_pairwise4_block_push(&s,input[i]);
    return m16_pairwise4_block_finish(&s);
}
typedef struct {unsigned a,b;} node;
static node old_nodes[2176],new_nodes[2176];
static unsigned old_work[1088],new_work[11];
static int same_tree(unsigned a,unsigned b,unsigned leaves){
    if(a<leaves || b<leaves)return a==b;
    return same_tree(old_nodes[a].a,new_nodes[b].a,leaves) && same_tree(old_nodes[a].b,new_nodes[b].b,leaves);
}
static void check_tree(unsigned n,unsigned grouped){
    unsigned i,j,count=n,next=n,root=0,have=0,used=0;
    for(i=0;i<n;i++)old_work[i]=i;
    while(count>1){unsigned pairs=count/2;
        for(j=0;j<pairs;j++){old_nodes[next]=(node){old_work[2*j],old_work[2*j+1]};old_work[j]=next++;}
        if(count&1u)old_work[pairs]=old_work[count-1];count=pairs+(count&1u);
    }
    next=n;
    for(i=0;i<n;){
        unsigned value,level=0,occupied=used;
        if(grouped){
            unsigned block[16],k,width=n-i<16?n-i:16;
            for(k=0;k<width;k++)block[k]=i+k;i+=width;
            while(width>1){unsigned pairs=width/2;for(k=0;k<pairs;k++){new_nodes[next]=(node){block[2*k],block[2*k+1]};block[k]=next++;}if(width&1u)block[pairs]=block[width-1];width=pairs+(width&1u);}
            value=block[0];
        }else value=i++;
        while(occupied&1u){new_nodes[next]=(node){new_work[level],value};value=next++;occupied>>=1;level++;}
        new_work[level]=value;used++;
    }
    for(i=0;i<11;i++)if(used&(1u<<i)){
        if(!have){root=new_work[i];have=1;}
        else{new_nodes[next]=(node){new_work[i],root};root=next++;}
    }
    checks++;if(!same_tree(old_work[0],root,n))errors++;
}
static void fill(unsigned pattern,unsigned n){
    unsigned i,j,state=20261023u+pattern;union{v4 f;uint32_t u[4];} value;
    for(i=0;i<n;i++){
        for(j=0;j<4;j++){
            state=1664525u*state+1013904223u;
            if(pattern%4==0)value.u[j]=(state&0x807fffffu)|((1u+(state>>24)%126u)<<23);
            else if(pattern%4==1)value.u[j]=((i+j)&1?0xbf800000u:0x3f800000u)+(state&15u);
            else if(pattern%4==2)value.u[j]=(state&0x80000000u)|(state&0x7fffffu);
            else value.u[j]=((i+j+pattern)&1)?0x80000000u:0;
        }
        input[i]=value.f;
    }
}
static double timed(unsigned mode,unsigned count,unsigned repeats){
    unsigned i;LARGE_INTEGER start,end,freq;v4 value=dup4(0.f);QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);
    for(i=0;i<repeats;i++){input[0][0]=(i&1u)?-0x1p-4f:0x1p-4f;value=mode==2?block_sum(count):mode?stream_sum(count):original_sum(count);checksum+=value[i&3u]*0x1p-20f;}
    QueryPerformanceCounter(&end);return (double)(end.QuadPart-start.QuadPart)/(double)freq.QuadPart;
}
int main(void){
    static const unsigned counts[]={1,2,3,4,5,7,8,15,16,17,31,32,33,64,65,128,255,256,257,512,513,800,1024,1088};
    unsigned n,k,p,r,order;
    for(n=1;n<=1088;n++){check_tree(n,0);check_tree(n,1);}
    for(k=0;k<sizeof(counts)/sizeof(counts[0]);k++)for(p=0;p<128;p++){
        v4 a,b,c;n=counts[k];fill(p,n);a=original_sum(n);b=stream_sum(n);c=block_sum(n);checks+=2;errors+=memcmp(&a,&b,sizeof(a))!=0;errors+=memcmp(&a,&c,sizeof(a))!=0;
    }
    printf("PAIRWISE_TREE_PASS checks=%u errors=%u original_terms_bytes=%u stream_state_bytes=%u\n",checks,errors,(unsigned)(1088*sizeof(v4)),(unsigned)sizeof(m16_pairwise4_stream));
    if(errors)return 1;
    printf("PAIRWISE_BLOCK_STATE bytes=%u\n",(unsigned)sizeof(m16_pairwise4_block_stream));
    fill(1,1088);
    for(p=0;p<3;p++)actual_calls[p]=0;
    for(r=0;r<9;r++)for(order=0;order<3;order++){
        unsigned mode=(r&1u)?2u-order:order;
        printf("PAIRWISE_TIME rep=%u mode=%u seconds=%.9f host_only=1\n",r,mode,timed(mode,1088,32768));
    }
    for(p=0;p<3;p++)if(actual_calls[p]!=9u*32768u){printf("BENCHMARK_CALL_COUNT_BAD mode=%u count=%u\n",p,actual_calls[p]);return 2;}
    puts("PAIRWISE_BENCHMARK_CALLS_CHECKED");
    return 0;
}
