#ifndef CM_COMBAT_INTERNAL_H
#define CM_COMBAT_INTERNAL_H

#include "ai.h"

#define AI_MISSILE_TYPES 53

typedef struct {
    int32_t fxo, sound, volume, aim, speed;
} ai_missile_data;

extern const ai_missile_data ai_missile_types[AI_MISSILE_TYPES];

#endif
