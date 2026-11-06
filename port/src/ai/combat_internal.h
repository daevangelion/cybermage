#ifndef CM_COMBAT_INTERNAL_H
#define CM_COMBAT_INTERNAL_H

#include "ai.h"

static inline int32_t ai_signed32(uint32_t n)
{
    return n <= UINT32_C(0x7fffffff) ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static inline int32_t ai_add32(int32_t a, int32_t b)
{
    return ai_signed32((uint32_t)a + (uint32_t)b);
}
static inline int32_t ai_sub32(int32_t a, int32_t b)
{
    return ai_signed32((uint32_t)a - (uint32_t)b);
}
static inline int32_t ai_scale32(int32_t a, int32_t b, int32_t divisor)
{
    return divisor ? ai_signed32((uint32_t)((int64_t)a * b / divisor)) : INT32_C(2147483647);
}
static inline int32_t ai_fixed32(int32_t a, int32_t b)
{
    return ai_signed32((uint32_t)((uint64_t)((int64_t)a * b) >> 16));
}
int ai_set_missile(world *w, unsigned object, unsigned fxo);

#endif
