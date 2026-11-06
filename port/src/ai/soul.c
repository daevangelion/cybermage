#include "ai.h"

/* CM_HI.EXE InitMissiles 0x2DB40: twelve records, original stride 0x30. */
int ai_init_missiles(ai_state *a, world *w)
{
    unsigned i;
    if (w->object_count + AI_MISSILE_MAX >= AI_DYNAMIC_FIRST)
        return cm_fail("InitMissiles", w->object_count, "object pool exhausted");
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        unsigned object = w->object_count++;
        a->missiles[i].active = 0;
        a->missiles[i].type = 0;
        a->missiles[i].object = (int32_t)object;
        w->objects[object].active = 0;
    }
    return 0;
}

/* InitSoulFire 0x2DC18: ten 0x1C records, each with a runtime PROP record. */
int ai_init_soul_fire(ai_state *a, world *w)
{
    unsigned i;
    if (w->object_count >= AI_DYNAMIC_FIRST - AI_SOUL_MAX ||
        w->prop_count >= 2048 - AI_SOUL_MAX)
        return cm_fail("InitSoulFire", w->object_count, "object pool exhausted");
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        unsigned object = w->object_count++, prop = w->prop_count++;
        a->souls[i] = (ai_soul){0};
        a->souls[i].object = (int32_t)object;
        a->souls[i].prop = (int32_t)prop;
        w->prop_objects[prop] = (uint16_t)object;
        w->prop_items[prop] = 0;
        w->objects[object].person = (int16_t)prop;
        w->objects[object].targetable = 2;
        w->objects[object].active = 0;
    }
    return 0;
}

/* MakeSoulFire 0x2DD9C; full pool drops the request without consuming RNG. */
int ai_make_soul_fire(ai_state *a, world *w, unsigned source, int32_t type,
                      int32_t value, int32_t lifetime, int on_floor)
{
    unsigned i;
    ai_soul *s;
    world_object *o;
    const world_object *from;
    if (source >= w->object_count)
        return cm_fail("MakeSoulFire", source, "invalid source object");
    for (i = 0; i < AI_SOUL_MAX && a->souls[i].active > 0; ++i) {}
    if (i == AI_SOUL_MAX) return 0;
    s = &a->souls[i]; o = &w->objects[s->object]; from = &w->objects[source];
    if (type >= 1 && type <= 3) {
        unsigned prop = type == 3 ? 30 : 13, template_object;
        const world_object *t;
        if (prop >= w->prop_count || w->prop_objects[prop] >= w->object_count)
            return cm_fail("SetSoulFire", prop, "invalid template PROP");
        template_object = w->prop_objects[prop];
        t = &w->objects[template_object];
        o->flags = t->flags & ~UINT32_C(0x00000800);
        o->radius = t->radius; o->height = t->height; o->z = t->z;
        o->texture = t->texture; o->rotations = t->rotations;
    }
    w->prop_items[s->prop] = (int16_t)value;
    s->type = type; s->value = value; s->lifetime = lifetime;
    s->active = 1; s->frame = 0;
    o->heading = 0; o->x = from->x; o->y = from->y;
    o->floor_z = (int32_t)((uint32_t)from->floor_z + (on_floor ? 0u : (uint32_t)from->z));
    o->active = 1;
    world_omap_insert(w, (unsigned)s->object);
    return 0;
}

void ai_update_soul_fire(ai_state *a, sim_state *s, world *w)
{
    unsigned i;
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        ai_soul *p = &a->souls[i];
        world_object *o = &w->objects[p->object];
        if (!p->active) continue;
        if (!o->active) { p->active = 0; continue; }
        if (!s->bp_limit_flag) continue;
        if (p->type >= 1 && p->type <= 3) {
            ++o->texture; p->frame = (p->frame + 1) % 4;
            if (!p->frame) o->texture -= 4;
        }
        if (p->lifetime && --p->lifetime <= 0) {
            p->active = 0; o->active = 0;
            world_omap_delete(w, (unsigned)p->object);
        }
    }
}
