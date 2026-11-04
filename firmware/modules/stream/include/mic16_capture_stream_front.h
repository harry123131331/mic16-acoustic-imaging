#ifndef MIC16_CAPTURE_STREAM_FRONT_H
#define MIC16_CAPTURE_STREAM_FRONT_H
#include "mic16_capture_stream.h"
#include "mic16_frame_front_stream.h"
#include "mic16_front_context.h"
int m16_front_capture_start(m16_capture_stream *);
int m16_front_capture_step(m16_capture_stream *);
int m16_front_capture_take(m16_capture_stream *,m16_capture_stream_view *);
int m16_front_capture_release(m16_capture_stream *,const m16_capture_stream_view *);
int m16_front_capture_finish(m16_capture_stream *);
int m16_front_capture_input_complete(const m16_capture_stream *);
int m16_front_capture_stop(m16_capture_stream *,int);
int m16_front_capture_releasable(const m16_capture_stream *);
int m16_front_capture_restart(m16_capture_stream *,unsigned,uint64_t,uint64_t);
#endif
