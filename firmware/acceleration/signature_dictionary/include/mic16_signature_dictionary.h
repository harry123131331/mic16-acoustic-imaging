#ifndef MIC16_SIGNATURE_DICTIONARY_H
#define MIC16_SIGNATURE_DICTIONARY_H
#include "mic16_spatial.h"
/* 0原库，1保留FP64短递推，2精确只读字典；仅在窗口边界选择。 */
extern unsigned m16_signature_dictionary_mode,m16_signature_dictionary_calls[3];
int m16_signature_dictionary_select(unsigned);
int m16_signature_dictionary_lookup(unsigned,unsigned,unsigned,float *);
#ifndef M16_SIGNATURE_DICTIONARY_ARM_WRAP
int m16_signature_dictionary_host_load(const char *);
#endif
#endif
