#ifndef CM_MUSIC_INTERNAL_H
#define CM_MUSIC_INTERNAL_H
#include "cm.h"
void music_init(unsigned rate);
void music_frame(int32_t *left, int32_t *right);
void synth_init(unsigned rate);
int synth_ready(void);
void synth_reset(void);
void synth_event(unsigned status, unsigned a, unsigned b);
void synth_frame(int32_t *left, int32_t *right);
void synth_volume(unsigned volume);
#endif
