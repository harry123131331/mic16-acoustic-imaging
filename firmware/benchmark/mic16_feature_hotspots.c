#include "mic16_feature_hotspots.h"
/* 单核串行诊断，保留有界嵌套栈；不用于中断或多线程。 */
typedef struct {unsigned id;uint64_t start,children;} scope;
static scope stack[8];
static mic16_hotspot_report report;
static mic16_hotspot_clock clock_read;
static void *clock_context;
static uint64_t read_tick(void){
 uint64_t t=clock_read(clock_context);
 if(report.reads && t<report.last_tick)report.errors++;
 report.last_tick=t;report.reads++;return t;
}
void mic16_hotspot_reset(mic16_hotspot_clock clock,void *context){
 report=(mic16_hotspot_report){0};clock_read=clock;clock_context=context;
 report.enabled=clock!=0;
}
void mic16_hotspot_begin(unsigned id){
 uint64_t t;if(!report.enabled)return;
 if(id>=M16_HOT_COUNT || report.depth>=8){report.errors++;report.enabled=0;return;}
 t=read_tick();stack[report.depth++]=(scope){id,t,0};
 if(report.depth>report.max_depth)report.max_depth=report.depth;
 report.items[id].calls++;
}
void mic16_hotspot_end(unsigned id){
 scope *s;uint64_t t,duration;
 if(!report.enabled)return;
 if(!report.depth || id>=M16_HOT_COUNT || stack[report.depth-1].id!=id){report.errors++;report.enabled=0;return;}
 t=read_tick();s=stack+report.depth-1;
 if(t<s->start || t-s->start<s->children){report.errors++;report.enabled=0;return;}
 duration=t-s->start;
 report.items[id].inclusive_ticks+=duration;
 report.items[id].exclusive_ticks+=duration-s->children;
 report.depth--;if(report.depth)stack[report.depth-1].children+=duration;
}
int mic16_hotspot_finish(mic16_hotspot_report *out){
 if(report.depth)report.errors++;
 report.enabled=0;
 if(!out)return -1;
 *out=report;return report.errors?-1:0;
}
