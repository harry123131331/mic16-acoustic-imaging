#ifndef MIC16_RESPONSE_RECURRENCE_H
#define MIC16_RESPONSE_RECURRENCE_H
extern unsigned m16_response_recurrence_mode,m16_response_recurrence_calls[4];
int m16_response_recurrence_select(unsigned);
void m16_response_recurrence_begin(const float *);
void m16_response_recurrence_block(const float *,unsigned,unsigned,float *);
#endif
