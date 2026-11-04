#include "mic16_output_wire_task.h"
#include "mic16_heatmap_wire.h"
/* 只暴露ABI与阶段，不在测试辅助层重写生产算法。 */
unsigned ow_size(unsigned kind){return kind?sizeof(m16_heatmap_wire_task):sizeof(m16_audio_wire_task);}
unsigned ow_info_size(void){return sizeof(m16_cont_info);}
unsigned ow_ticket_size(void){return sizeof(m16_wire_ticket);}
unsigned ow_phase(unsigned kind,const void *p){return kind?((const m16_heatmap_wire_task *)p)->phase:((const m16_audio_wire_task *)p)->phase;}
unsigned ow_index(unsigned kind,const void *p){return kind?((const m16_heatmap_wire_task *)p)->index:((const m16_audio_wire_task *)p)->index;}
void ow_force_serial(unsigned kind,void *p,uint64_t value){if(kind)((m16_heatmap_wire_task *)p)->serial=value;else ((m16_audio_wire_task *)p)->serial=value;}
unsigned ow_reference_map_size(void){return sizeof(m16_heatmap_workspace);}
