#include "mic16_capture_session.h"
void mic16_session_init(mic16_capture_session *s) {
    if(!s)return;
    *s=(mic16_capture_session){.phase=M16_SESSION_QUIET};
}
int mic16_session_packet(mic16_capture_session *s,int command,unsigned int *slot) {
    if(!s || !slot || s->phase<M16_SESSION_QUIET || s->phase>=M16_SESSION_DONE ||
       s->stored>=M16_SESSION_TOTAL_PACKETS)return -1;
    if(command=='X' || command=='x'){s->phase=M16_SESSION_FAILED;return -1;}
    if(command=='A' || command=='a') {
        if(s->phase==M16_SESSION_QUIET)s->early_start++;
        else if(s->phase==M16_SESSION_WAIT_AUDIO) {
            s->phase=M16_SESSION_AUDIO;s->audio_first_sequence=s->received;
        }
    }
    s->received++;
    /* 固定工程预热时间不等于已经测量并保证麦克风稳定时间。 */
    if(s->phase==M16_SESSION_QUIET && s->warmup<M16_SESSION_WARMUP_PACKETS) {
        s->warmup++;return 0;
    }
    if(s->phase==M16_SESSION_WAIT_AUDIO){s->discarded++;return 0;}
    *slot=s->stored++;
    if(s->stored==M16_SESSION_REFERENCE_PACKETS)s->phase=M16_SESSION_WAIT_AUDIO;
    else if(s->stored==M16_SESSION_TOTAL_PACKETS){s->phase=M16_SESSION_DONE;return 2;}
    return 1;
}
