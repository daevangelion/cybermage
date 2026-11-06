#include "ai.h"

/* NewMONBound steering half, CM_HI.EXE 0x46507..0x46A74. */
int ai_monster_blocked(ai_state *a, sim_state *s, world *w, unsigned object,
                       unsigned collisions)
{
    world_object *o;
    world_person *p;
    int32_t qx, qy, tx, ty, rx, ry;
    unsigned tile, region, quadrant;
    uint16_t mask, heading;
    int right, up, left, down;
    const world_aperture *ap;
    if (object >= w->object_count)
        return cm_fail("NewMONBound", object, "invalid object");
    o = &w->objects[object];
    if (o->person < 0 || (unsigned)o->person >= w->person_count)
        return cm_fail("NewMONBound", object, "invalid PERSON association");
    p = &w->persons[o->person];
    if (o->target_mode == 2) {
        if (collisions) {
            if (p->target >= 0) {
                if ((unsigned)p->target >= w->person_count)
                    return cm_fail("NewMONBound", object, "invalid target");
                ai_track_target(w, object, (unsigned)w->persons[p->target].object, 0);
            }
            p->unk_0020 = 16;
        }
        return 0;
    }
    if (!collisions && (o->target_mode || world_rnd(&w->rng, 512) != 256)) return 0;
    qx = o->x / 0x4000; qy = o->y / 0x4000;
    tx = qx >> 2; ty = qy >> 2;
    rx = qx - tx * 4; ry = qy - ty * 4;
    if (tx < 0 || tx >= CM_MAP_SIDE || ty < 0 || ty >= CM_MAP_SIDE ||
        s->collision_level < 0 || s->collision_level >= CM_MAP_LEVELS)
        return cm_fail("NewMONBound", object, "actor outside collision map");
    tile = (unsigned)(ty * CM_MAP_SIDE + tx);
    mask = s->collision_mode ? w->inertia_mask[s->collision_level][tile] :
                              w->path_mask[s->collision_level][tile];
    right = (mask & (1u << (ry * 4 + 3))) != 0;
    up = (mask & (1u << (rx + 12))) != 0;
    left = (mask & (1u << (ry * 4))) != 0;
    down = (mask & (1u << rx)) != 0;
    region = w->cells[s->collision_level][tile].region;
    if (region >= SIM_APERTURE_MAX)
        return cm_fail("NewMONBound", region, "invalid aperture");
    ap = &w->apertures[region];
    if (ap->enabled) {
        if ((ap->min_x >> 16) < tx) left = 0;
        else if ((ap->max_x >> 16) > tx) right = 0;
        else if ((ap->min_y >> 16) < ty) down = 0;
        else if ((ap->max_y >> 16) > ty) up = 0;
    }
    heading = o->heading;
    if (o->target_mode) {
        quadrant = (uint16_t)(o->target_angle + 0x2000) / 0x4000;
        if (!(quadrant == 0 ? right : quadrant == 1 ? up : quadrant == 2 ? left : down)) return 0;
    } else if ((heading & 0x3FFF) && !(s->collision_flags & 2)) {
        o->target_mode = 1; p->unk_006C = 0;
        quadrant = (uint16_t)(heading + 0x2000) / 0x4000;
        if (!(quadrant == 0 ? right : quadrant == 1 ? up : quadrant == 2 ? left : down)) {
            o->target_angle = (uint16_t)(quadrant * 0x4000); return 0;
        }
    }
    o->target_mode = 1; p->unk_006C = 0;
    quadrant = (uint16_t)(heading + 0x2000) / 0x4000;
    if (quadrant & 1) {
        if (!right && (left || world_rnd(&w->rng, 2))) o->target_angle = 0;
        else if (!left) o->target_angle = 0x8000;
        else if (up && down) {
            if (heading == o->target_angle) o->target_angle = (uint16_t)(heading + 0x4000);
        } else if (quadrant == 1) {
            if ((up || s->collision_flags) && !down) o->target_angle = 0xC000;
            else o->target_angle = (uint16_t)(heading + (rx > 1 ? 0x1000 : -0x1000));
        } else {
            if ((down || s->collision_flags) && !up) o->target_angle = 0x4000;
            else o->target_angle = (uint16_t)(heading + (rx > 1 ? -0x1000 : 0x1000));
        }
    } else {
        if (!up && (down || world_rnd(&w->rng, 2))) o->target_angle = 0x4000;
        else if (!down) o->target_angle = 0xC000;
        else if (right && left) {
            if (heading == o->target_angle) o->target_angle = (uint16_t)(heading + 0x4000);
        } else if (quadrant == 0) {
            if ((right || s->collision_flags) && !left) o->target_angle = 0x8000;
            else o->target_angle = (uint16_t)(heading + (ry > 1 ? -0x1000 : 0x1000));
        } else {
            if ((left || s->collision_flags) && !right) o->target_angle = 0;
            else o->target_angle = (uint16_t)(heading + (ry > 1 ? 0x1000 : -0x1000));
        }
    }
    (void)a;
    return 0;
}
