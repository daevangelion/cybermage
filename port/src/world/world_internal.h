#ifndef CM_WORLD_INTERNAL_H
#define CM_WORLD_INTERNAL_H
#include "world.h"
#include <limits.h>

static inline int16_t world_s16(const uint8_t *p)
{
    uint16_t v = cm_u16(p);
    return v <= INT16_MAX ? (int16_t)v : (int16_t)(-1 - (int)(UINT16_MAX - v));
}
static inline int32_t world_s32(const uint8_t *p)
{
    uint32_t v = cm_u32(p);
    return world_wrap32(v);
}
static inline int32_t world_mul16(int32_t a, int32_t b)
{
    uint32_t v = (uint32_t)((uint64_t)((int64_t)a * b) >> 16);
    return world_wrap32(v);
}
int world_load_panels(world *w);
int world_load_placements(world *w);
int world_load_persons(world *w);
int world_load_gsx(world *w, unsigned id);
int world_load_gsx_flags(world *w, unsigned id, unsigned mode);
int world_texture_package(world *w, unsigned kind, unsigned resource,
                          unsigned packed, unsigned scale, unsigned *base);
int world_prop_texture(world *w, const uint8_t *definition, unsigned *base);
uint32_t world_section_offset(const world *w, unsigned section);
int world_section_records(world_span span, unsigned stride, const char *path,
                            uint32_t offset, unsigned *count);
int world_path(char *out, const char *directory, const char *name);
#endif
