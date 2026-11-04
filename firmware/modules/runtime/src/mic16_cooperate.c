#include "../../mic16_cooperate.h"
#if defined(MIC16_COOPERATIVE_IO) && MIC16_COOPERATIVE_IO
static void (*callback)(void *);
static void *argument;
static unsigned active;
int m16_cooperate_bind(void (*service)(void *),void *context){
 if(active)return -1;
 callback=service;argument=context;return 0;
}
void m16_cooperate(void){
 if(!callback||active)return;
 active=1;callback(argument);active=0;
}
#endif
