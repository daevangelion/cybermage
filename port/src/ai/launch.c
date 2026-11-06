#include "combat_internal.h"
#include "audio.h"

#include <limits.h>

/* CM_HI.EXE LaunchMissile 0x2DFD0; slots 0..3 reserved for the player. */
static int32_t add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}
static int32_t scaled(int32_t a, int32_t b, int32_t divisor)
{
    return divisor ? (int32_t)((int64_t)a * b / divisor) : INT32_MAX;
}
static int32_t cosine(const world *w, uint16_t angle, int32_t radius)
{
    return (int32_t)((int64_t)w->trig.cos[angle / 32] * radius >> 16);
}
static int32_t sine(const world *w, uint16_t angle, int32_t radius)
{
    return (int32_t)((int64_t)w->trig.sin[angle / 32] * radius >> 16);
}
static void position(const world *w, world_object *o, const world_object *from,
                     uint16_t angle, int32_t radius)
{
    o->x = add(from->x, cosine(w, angle, radius));
    o->y = add(from->y, sine(w, angle, radius));
}
/* SetMissile02DAD7 reads Xox+131BCC and its PROP object word at descriptor+4.
 * Placed FXO templates belong to a different table. */
int ai_set_missile(world *w, unsigned object, unsigned prop)
{
    world_object *o;
    const world_object *t;
    if (object >= w->object_count || prop >= w->prop_count ||
        w->prop_objects[prop] >= w->object_count)
        return cm_fail("SetMissile", prop, "invalid object or template PROP");
    o = &w->objects[object]; t = &w->objects[w->prop_objects[prop]];
    o->flags = (t->flags | UINT32_C(0x01000000)) & ~UINT32_C(0x00000800);
    o->radius = t->radius; o->height = t->height;
    o->texture = t->texture; o->rotations = t->rotations;
    return 0;
}

int ai_launch_missile(ai_state *a, sim_state *s, world *w, unsigned source,
                      unsigned target, unsigned type)
{
    const ai_missile_data *data;
    const world_object *from, *to;
    world_object *o;
    ai_missile *m;
    unsigned slot;
    int32_t source_z, target_z, distance;
    if (!a->tables_loaded)
        return cm_fail("LaunchMissile", 0, "combat tables not loaded");
    if (source >= w->object_count || target >= w->object_count || type >= AI_MISSILE_TYPES)
        return cm_fail("LaunchMissile", type, "invalid source, target or missile type");
    for (slot = 4; slot < AI_MISSILE_MAX && a->missiles[slot].active > 0; ++slot) {}
    if (slot == AI_MISSILE_MAX) return 0;
    m = &a->missiles[slot]; data = &a->missile_types[type];
    o = &w->objects[m->object]; from = &w->objects[source]; to = &w->objects[target];
    m->target = 0; m->source = (int32_t)source; m->frame = 0;
    m->sound = -1; m->variant = 0; m->speed = data->speed;
    source_z = from->z; target_z = add(to->z, to->z / 4);
    if (add(from->floor_z, 0x1100) < to->floor_z) target_z = add(target_z, to->z / 2);
    if (target == w->player_object) target_z = add(target_z, s->unk_144b34);
    else if (to->flags & UINT32_C(0x00001000)) target_z = add(target_z, -to->z / 2);
    if (source != w->player_object && (from->flags & UINT32_C(0x00001000))) source_z = 0;
    if (type == 1 || type == 38) {
        if (ai_set_missile(w, (unsigned)m->object, type == 1 ? 3u : 4u)) return -1;
        o->heading = (uint16_t)(cm_atan64k(from->x, from->y, to->x, to->y) +
                     world_rnd(&w->rng, type == 1 ? 0x1000 : 0x800) - (type == 1 ? 0x800 : 0x400));
        position(w, o, from, from->heading, 0x2000);
        o->floor_z = add(add(from->floor_z, source_z), type == 1 ? from->height : from->height / 4);
        o->z = 0;
        distance = cm_distance(o->x, o->y, to->x, to->y);
        if (type == 38) {
            m->ticks = 20;
            m->vertical = scaled(add(add(to->floor_z, target_z), -o->floor_z), m->speed, distance);
        } else {
            int32_t range = add(distance, 0x4000), vertical;
            if (range > 0x24000) range = 0x24000;
            else if (range < 0x8000) range = 0x8000;
            if (to->floor_z > from->floor_z) {
                vertical = scaled(add(add(to->floor_z, target_z), -o->floor_z), 0x4000, range - 0x4000);
                if (vertical < 0) vertical = -vertical;
                m->ticks = (range - 0x8000) / 0x4000;
            } else if (add(to->floor_z, 0x8000) < from->floor_z) {
                o->floor_z = add(from->floor_z, from->height / 2); m->speed = 0x1000;
                vertical = add(from->floor_z, -to->floor_z) >> ((unsigned)(distance / 0x1000) & 31);
                m->ticks = 0;
            } else {
                vertical = add(source_z, from->height) >> ((unsigned)(range / 0x4000) & 31);
                m->ticks = 0;
            }
            if (vertical < 64) vertical = 64;
            m->vertical = vertical;
        }
    } else if (type == 42) {
        if (ai_set_missile(w, (unsigned)m->object, 7)) return -1;
        o->flags |= UINT32_C(0x00008000);
        o->heading = (uint16_t)(target * 0x2000u);
        o->x = from->x; o->y = from->y; o->floor_z = from->floor_z; o->z = 0;
        m->ticks = 1; m->vertical = 0; m->frame = 0; m->radius = 0xA000;
    } else if (type == 36 || type == 52) {
        if (ai_set_missile(w, (unsigned)m->object, type == 36 ? 7u : (unsigned)data->fxo)) return -1;
        o->flags |= UINT32_C(0x00008000); o->heading = from->heading;
        if (type == 52) position(w, o, from, from->heading, 0x2000);
        else { o->x = from->x; o->y = from->y; }
        o->floor_z = add(add(from->floor_z, source_z), type == 36 ? 0x800 : from->height / 4);
        o->z = 0; snd_object(w, 14, (unsigned)m->object, 0x60000, 0x9FFF, 0);
        m->variant = type == 52 ? 7 : 0; m->ticks = 3;
        m->vertical = type == 52 ? 4 : 6; m->radius = type == 52 ? 0xA000 : 0x4000;
        if (type == 36) type = 5;
    } else if (type == 33) {
        if (data->sound) snd_play((unsigned)data->sound, 0x7FFF, 0x8000);
        if (ai_set_missile(w, (unsigned)m->object, (unsigned)data->fxo)) return -1;
        m->target = (int32_t)target; m->source = 0; m->frame = (int32_t)(slot % 4);
        o->texture = (uint16_t)(o->texture + m->frame);
        if (target == w->player_object) {
            o->heading = to->heading;
            if (a->unk_144AE0) o->heading = (uint16_t)(o->heading + 0x1800 - world_rnd(&w->rng, 6) * 0x800);
        } else o->heading = (uint16_t)(world_rnd(&w->rng, 16) * 0x1000);
        position(w, o, to, o->heading, 0x2800);
        o->floor_z = add(add(to->floor_z, source_z), to->height / 4); o->z = 0;
        m->vertical = 30; m->variant = 11; m->ticks = 1;
    } else if (type == 46 || type == 48) {
        uint16_t xangle, yangle;
        if (ai_set_missile(w, (unsigned)m->object, (unsigned)data->fxo)) return -1;
        m->target = (int32_t)target;
        if (type == 48 && w->person_count > 1) m->source = w->persons[1].object;
        if (type == 46) {
            xangle = (uint16_t)(from->heading + 0x4000 + world_rnd(&w->rng, 2) * 0x8000);
            yangle = (uint16_t)(from->heading + 0x4000 + world_rnd(&w->rng, 2) * 0x8000);
        } else {
            xangle = (uint16_t)(from->heading + world_rnd(&w->rng, 8) * 0x2000);
            yangle = (uint16_t)(from->heading + world_rnd(&w->rng, 8) * 0x2000);
        }
        o->x = add(from->x, cosine(w, xangle, 0x1000));
        o->y = add(from->y, sine(w, yangle, 0x1000));
        o->heading = (uint16_t)(cm_atan64k(o->x, o->y, to->x, to->y) + world_rnd(&w->rng, 0x1000) - 0x800);
        o->floor_z = add(from->floor_z, type == 46 ? 0x800 : 0); o->z = 0;
        if (data->sound) snd_object(w, (unsigned)data->sound, (unsigned)m->object,
                                   0x60000, 0x7FFF + data->volume, 0);
        distance = cm_distance(o->x, o->y, to->x, to->y);
        if (!m->speed) return cm_fail("LaunchMissile", type, "zero homing missile speed");
        m->vertical = scaled(add(o->floor_z, -add(to->floor_z, 0x1000)), m->speed, distance);
        m->ticks = 0x40000 / m->speed + 1;
        if (type == 48) type = 46;
    } else {
        if (ai_set_missile(w, (unsigned)m->object, (unsigned)data->fxo)) return -1;
        if ((type >= 25 && type <= 27) || type == 39) o->flags |= UINT32_C(0x00008000);
        if (data->aim == 0) o->heading = (uint16_t)(cm_atan64k(from->x, from->y, to->x, to->y) + world_rnd(&w->rng, 0x1000) - 0x800);
        else if (data->aim == 1) o->heading = (uint16_t)(from->heading + world_rnd(&w->rng, 0x400) - 0x200);
        else if (data->aim == 2) o->heading = (uint16_t)(cm_atan64k(from->x, from->y, to->x, to->y) + world_rnd(&w->rng, 0x800) - 0x400);
        position(w, o, from, from->heading, 0x2000);
        o->floor_z = add(add(from->floor_z, source_z), from->height / 4); o->z = 0;
        if (data->sound) snd_object(w, (unsigned)data->sound, (unsigned)m->object,
                                   0x60000, 0x7FFF + data->volume, 0);
        distance = cm_distance(o->x, o->y, to->x, to->y);
        if (!m->speed) return cm_fail("LaunchMissile", type, "zero missile speed");
        m->vertical = scaled(add(o->floor_z, -add(to->floor_z, target_z)), m->speed, distance);
        m->ticks = 0x40000 / m->speed + 1;
        if (type == 37) type = 9;
    }
    o->active = 1; m->active = 1; m->type = (int32_t)type;
    world_omap_insert(w, (unsigned)m->object);
    return 0;
}
