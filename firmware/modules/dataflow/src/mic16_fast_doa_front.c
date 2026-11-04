#include "mic16_fast_doa_front.h"
int m16_fast_doa_front_step(m16_fast_doa_front *f,uint64_t tick){
 const float *map;const m16_front_frame_metadata *m;unsigned slot;int rc;
 if(!f||!f->ring||!f->task)return M16_FD_INVALID;
 rc=m16_fast_doa_step(f->task,tick);if(rc)return rc;
 if(!f->task->active)return 2;
 if(!m16_fast_doa_can_accept(f->task)){f->output_waits++;return 1;}
 if(f->ring->epoch!=f->task->epoch)return M16_FD_SEQUENCE;
 if(f->task->next_frame>=f->ring->next_frame){f->cache_waits++;return 0;}
 slot=(unsigned)(f->task->next_frame%20u);m=&f->ring->metadata[slot];
 if(!m->ready||m->epoch!=f->task->epoch||m->frame_seq!=f->task->next_frame){f->overwritten++;return M16_FD_SEQUENCE;}
 rc=m16_spatial_evidence_coarse(&f->ring->evidence,m->epoch,m->frame_seq,&map);if(rc)return rc;
 rc=m16_fast_doa_begin(f->task,map,f->ring->storage->spectrum[slot].zone_level_db,
  m->epoch,m->frame_seq,m->first_sample,m->received_tick,m->front_done_tick);
 if(!rc)f->copied_frames++;
 return rc?rc:1;
}
