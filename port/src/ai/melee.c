#include "ai.h"

static int32_t add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

/* Xox::H2HTarget, CM_HI.EXE 0x19EA4. Result is PERSON index, zero on miss. */
int32_t ai_h2h_target(ai_state *a, const sim_state *s, const world *w,
                      unsigned source, unsigned sword)
{
    unsigned i;
    int32_t selected = 0, low, high;
    const world_object *o;
    if (source >= w->object_count || sword >= 3) {
        cm_fail("H2HTarget", source, "invalid source or sword");
        return -1;
    }
    o = &w->objects[source];
    high = add(add(o->floor_z, (int32_t)((uint32_t)o->height * 2u)), s->unk_144b34);
    low = add(o->floor_z, s->unk_144b34);
    a->h2h_distance = 0x6000;
    for (i = 1; i < w->person_count; ++i) {
        const world_person *p = &w->persons[i];
        const world_object *t;
        int32_t dx, dy, distance, delta;
        uint16_t angle;
        if (p->status == 2) continue;
        if (p->object < 0 || (unsigned)p->object >= w->object_count) {
            cm_fail("H2HTarget", i, "invalid target object"); return -1;
        }
        t = &w->objects[p->object];
        dx = add(o->x, -t->x); dy = add(o->y, -t->y);
        if (dx <= -0x6000 || dx >= 0x6000 || dy <= -0x6000 || dy >= 0x6000 ||
            high <= t->floor_z || add(t->floor_z, (int32_t)((uint32_t)t->height * 2u)) <= low) continue;
        angle = cm_atan64k(o->x, o->y, t->x, t->y);
        delta = (int16_t)(angle - (uint16_t)(o->heading - (uint16_t)a->sword_range[sword]));
        if (delta <= -0xB00 || delta >= 0xB00) continue;
        distance = cm_distance(o->x, o->y, t->x, t->y);
        if (distance >= a->h2h_distance) continue;
        a->h2h_distance = distance; a->h2h_angle = (int16_t)delta;
        selected = (int32_t)i;
    }
    return selected;
}
