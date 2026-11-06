#include "sim_internal.h"
#include "ai.h"
#include "audio.h"

static int32_t clamp(int32_t n, int32_t low, int32_t high)
{
    return n < low ? low : (n > high ? high : n);
}

static int collision_level(sim_state *s, const world *w, int32_t z)
{
    int32_t level = (z + SIM_HEAD_CLEARANCE) >> 16;
    if (level > w->top_level) level = w->top_level;
    if (level < 0 || level >= CM_MAP_LEVELS)
        return cm_fail("MovePerson", (uint32_t)level, "invalid collision level");
    s->collision_level = level;
    return 0;
}

/* CM_HI.EXE 046E41..046ED6; signed division must precede SAR. */
static int32_t snap_axis(int32_t value)
{
    int32_t q = value / SIM_QUARTER_TILE;
    int32_t tile = (q >> 2) * SIM_TILE_UNIT;
    int32_t remainder = q - (q >> 2) * 4;
    if (remainder == 0) return tile + SIM_QUARTER_TILE;
    if (remainder == 3) return tile + 0xbfff;
    return value;
}

static void body_height(sim_state *s, const world_object *o)
{
    s->body_height = o->z + o->z / 2 + 0x1000 + s->unk_144b34;
}

/* Xox::MovePerson 046D30..047B8D. Heading is restored after physical
 * movement so strafing never changes the rendered facing direction. */
int sim_move_person(sim_state *s, world *w, unsigned object,
                    int32_t yaw, int32_t speed, int32_t vertical)
{
    world_object *o;
    world_person *p;
    int kind = 0, hit;
    int32_t x, y, old_z, floor, ceiling_z = 0;
    uint16_t angle;
    if (object >= w->object_count || w->objects[object].person < 0 ||
        (unsigned)w->objects[object].person >= w->person_count)
        return cm_fail("MovePerson", object, "invalid object PERSON");
    o = &w->objects[object];
    p = &w->persons[o->person];
    if (s->ai) kind = ai_person_kind(s->ai, w, (unsigned)o->person);
    s->support_object = s->unk_130d18 = 0;
    s->no_openings = kind == 17 || kind == 5;
    if (kind == 17) s->unk_130d18 = speed / 10 + 1;
    if (kind == 5) s->unk_130d18 = speed / 5 + 1;
    s->collision_mode = 1;
    if (collision_level(s, w, o->floor_z)) return -1;
    if (sim_path_check(s, w, o->x, o->y)) {
        o->x = snap_axis(o->x);
        o->y = snap_axis(o->y);
    }
    if (p->unk_007C && yaw > -32 && yaw < 32) {
        unsigned riding = (unsigned)p->unk_007C;
        if (riding >= w->object_count) return cm_fail("MovePerson", riding, "invalid riding object");
        o->heading = w->objects[riding].heading;
    } else if (yaw) {
        yaw = clamp(yaw, -SIM_YAW_MAX, SIM_YAW_MAX);
        o->heading = (uint16_t)(o->heading + yaw * 16 * s->frame_ticks / 2);
    }
    s->save_pc_vector = o->heading;
    o->heading = (uint16_t)(o->heading + s->movement_angle);
    x = o->x;
    y = o->y;
    if (speed) {
        int32_t distance;
        speed = clamp(speed, -SIM_SPEED_MAX, SIM_SPEED_MAX);
        distance = speed * 96 * s->frame_ticks / 2;
        angle = o->heading >> 5;
        x += sim_mul16(distance, w->trig.cos[angle]);
        y += sim_mul16(distance, w->trig.sin[angle]);
        if (sim_new_pc_bound(s, w, object, &x, &y)) return -1;
    }
    old_z = o->floor_z;
    if (old_z < ((old_z + 0x800) >> 16) * SIM_TILE_UNIT)
        old_z = ((old_z + 0x800) >> 16) * SIM_TILE_UNIT;
    if (s->flight_mode) {
        int32_t cap, before_x = x, before_y = y;
        int original_level = s->collision_level;
        if (s->unk_144b34) {
            s->unk_144b34 = s->unk_144b3c = 0;
            body_height(s, o);
        }
        if (sim_coll_flat(w, object, o->x, o->y)) return -1;
        floor = o->floor_z;
        o->floor_z = clamp(floor + s->unk_131418, old_z - 0x2000, old_z + 0x2000);
        hit = sim_coll_top_flat(s, w, object, old_z > floor ? old_z : floor, SIM_TOP_CLEARANCE);
        if (hit < 0) return -1;
        if (hit && o->floor_z < floor) {
            s->unk_144b34 = o->floor_z - floor;
            o->floor_z = floor;
            body_height(s, o);
        }
        cap = hit ? o->floor_z : 0;
        if (collision_level(s, w, o->floor_z) ||
            sim_new_pc_bound(s, w, object, &x, &y) ||
            collision_level(s, w, o->floor_z + s->body_height - SIM_HEAD_CLEARANCE) ||
            sim_new_pc_bound(s, w, object, &x, &y)) return -1;
        if (sim_path_check(s, w, x, y)) {
            o->floor_z = (original_level + 1) * SIM_TILE_UNIT - s->body_height - SIM_TOP_CLEARANCE - 1;
            if (o->floor_z < floor) o->floor_z = floor;
            if (cap && o->floor_z < cap) o->floor_z = cap;
            x = before_x; y = before_y;
            if (collision_level(s, w, o->floor_z) ||
                sim_new_pc_bound(s, w, object, &x, &y) ||
                collision_level(s, w, o->floor_z + s->body_height - SIM_HEAD_CLEARANCE) ||
                sim_new_pc_bound(s, w, object, &x, &y)) return -1;
        }
        if (cap && o->floor_z > cap) { o->floor_z = cap; x = o->x; y = o->y; }
    } else if (s->unk_144b1c) {
        int32_t before_x = x, before_y = y;
        int original_level = s->collision_level;
        if (sim_coll_flat(w, object, o->x, o->y)) return -1;
        floor = o->floor_z;
        if (s->unk_144b2c + vertical <= floor) {
            s->unk_144b1c = s->unk_144b28 = 0;
            s->unk_144b3c = 1;
        } else o->floor_z = s->unk_144b2c + vertical;
        hit = sim_coll_top_flat(s, w, object, old_z > floor ? old_z : floor, SIM_TOP_CLEARANCE);
        if (hit < 0) return -1;
        if (hit) {
            ceiling_z = o->floor_z;
            if (ceiling_z < floor) {
                o->floor_z = floor;
                s->unk_144b34 = ceiling_z - floor;
                body_height(s, o);
            }
        }
        if (collision_level(s, w, o->floor_z + s->body_height - SIM_HEAD_CLEARANCE) ||
            sim_new_pc_bound(s, w, object, &x, &y)) return -1;
        if (sim_path_check(s, w, x, y)) {
            o->floor_z = (original_level + 1) * SIM_TILE_UNIT - s->body_height - SIM_TOP_CLEARANCE - 1;
            if (o->floor_z < floor) o->floor_z = floor;
            if (o->floor_z < ceiling_z) o->floor_z = ceiling_z;
            x = before_x;
            y = before_y;
            if (collision_level(s, w, o->floor_z + s->body_height - SIM_HEAD_CLEARANCE) ||
                sim_new_pc_bound(s, w, object, &x, &y)) return -1;
        }
        if (collision_level(s, w, o->floor_z) || sim_new_pc_bound(s, w, object, &x, &y)) return -1;
        if (ceiling_z > o->floor_z) {
            s->unk_144b34 = ceiling_z - o->floor_z;
            body_height(s, o);
        }
    } else {
        int32_t saved_floor;
        if (sim_coll_flat(w, object, o->x, o->y)) return -1;
        if (s->support_object && s->pc_mode != 7) {
            const world_object *support = &w->objects[s->support_object];
            p->unk_0080 = (p->unk_007C ? p->unk_0080 : o->floor_z) + 0x800;
            if (p->unk_0080 > support->z + support->height) p->unk_0080 = support->z + support->height;
            p->unk_007C = s->support_object;
            o->floor_z = p->unk_0080;
        } else p->unk_007C = 0;
        if (old_z - SIM_QUARTER_TILE > o->floor_z) {
            o->floor_z = old_z - SIM_QUARTER_TILE * s->frame_ticks / 2;
            s->unk_144b30 = 1;
        } else if (s->unk_144b30) { s->unk_144b30 = 0; s->unk_144b3c = 1; }
        if (old_z > o->floor_z) {
            int32_t before_x = x, before_y = y;
            if (collision_level(s, w, o->floor_z) || sim_new_pc_bound(s, w, object, &x, &y)) return -1;
            if (before_x != x || before_y != y) {
                x = before_x;
                y = before_y;
                o->x = snap_axis(o->x);
                o->y = snap_axis(o->y);
                if (sim_new_pc_bound(s, w, object, &x, &y)) return -1;
            }
            o->x = x; o->y = y;
            if (sim_clear_object(s, w, object)) return -1;
            x = o->x; y = o->y;
        }
        saved_floor = o->floor_z;
        if (old_z > o->floor_z) o->floor_z = old_z;
        if (sim_coll_flat(w, object, x, y)) return -1;
        if (saved_floor > o->floor_z) o->floor_z = saved_floor;
        {
            int32_t crouch = sim_crouch_top_flat(s, w, object, s->unk_144b34, x, y);
            if (crouch != s->unk_144b34) {
                s->unk_144b34 = crouch;
                s->unk_144b3c = -1;
                /* MovePerson047A9F..047AFA: forced crouch plays sample0 and
                 * produces the next frame's hit shake, even without damage. */
                if (s->ai) {
                    if (snd_done(s->ai->pc_sound, 0))
                        s->ai->pc_sound = snd_play(0,
                            32767 - world_rnd(&w->rng, 0x4000), 32768);
                    if (s->ai->pc_flash < 2) s->ai->pc_flash = 2;
                }
                body_height(s, o);
            }
        }
    }
    o->heading = s->save_pc_vector;
    o->x = x;
    o->y = y;
    s->no_openings = s->unk_130d18 = 0;
    return 0;
}

/* Xox::MoveMonster 047B90. Actor speed uses 128 rather than the player
 * 96*ticks/2 scale; caller gates sequence speed on its animation tick. */
int sim_move_monster(sim_state *s, world *w, unsigned object, int32_t speed, int32_t vertical)
{
    world_object *o;
    world_person *p;
    int kind = 0, airborne, hit;
    int32_t x, y, old_z, floor;
    unsigned angle;
    if (object >= w->object_count || w->objects[object].person < 0 ||
        (unsigned)w->objects[object].person >= w->person_count)
        return cm_fail("MoveMonster", object, "invalid object PERSON");
    o = &w->objects[object];
    p = &w->persons[o->person];
    if (s->ai) kind = ai_person_kind(s->ai, w, (unsigned)o->person);
    airborne = kind == 9 || kind == 16 || kind == 10;
    s->support_object = s->unk_130d18 = 0;
    if (kind == 17 || kind == 5) s->no_openings = 1;
    else if (!airborne) s->no_openings = 0;
    s->collision_mode = p->unk_00A4 != 0;
    if (collision_level(s, w, o->floor_z)) return -1;
    if (sim_path_check(s, w, o->x, o->y)) { o->x = snap_axis(o->x); o->y = snap_axis(o->y); }
    if (p->unk_007C) {
        unsigned ride = (unsigned)p->unk_007C;
        if (ride >= w->object_count) return cm_fail("MoveMonster", ride, "invalid riding object");
        o->heading = w->objects[ride].heading;
    }
    s->save_pc_vector = o->heading;
    x = o->x; y = o->y;
    speed = clamp(speed, -SIM_SPEED_MAX, SIM_SPEED_MAX);
    if (speed) {
        angle = o->heading >> 5;
        x += sim_mul16(speed * 128, w->trig.cos[angle]);
        y += sim_mul16(speed * 128, w->trig.sin[angle]);
        if ((p->unk_00A4 ? sim_new_pc_bound(s, w, object, &x, &y) :
                           sim_monster_bound(s, w, object, &x, &y))) return -1;
    }
    old_z = o->floor_z;
    if (p->unk_0060 || p->unk_00A4) {
        int original_level = s->collision_level;
        int32_t cap, body = o->height * 2, before_x = x, before_y = y;
        if (body < SIM_BODY_MIN) body = SIM_BODY_MIN;
        if (p->unk_00A4 > 0 && (x >> 9) == (o->x >> 9) && (y >> 9) == (o->y >> 9)) {
            p->unk_00A4 = -1;
            p->unk_00AC = w->unk_5E7A8 ? 0x4cc : 0x800;
        }
        if (sim_coll_flat(w, object, o->x, o->y)) return -1;
        floor = o->floor_z;
        o->floor_z = clamp(floor + vertical, old_z - 0x2000, old_z + 0x2000);
        hit = sim_coll_top_flat(s, w, object, old_z > floor ? old_z : floor, SIM_TOP_CLEARANCE);
        if (hit < 0) return -1;
        if (hit) {
            if (o->floor_z < floor) o->floor_z = floor;
            if (p->unk_0060) p->unk_0060 = o->floor_z - floor;
        }
        cap = hit ? o->floor_z : 0;
        s->collision_mode = p->unk_00A4 != 0;
        if (collision_level(s, w, o->floor_z) ||
            sim_new_pc_bound(s, w, object, &x, &y) ||
            collision_level(s, w, o->floor_z + body - SIM_HEAD_CLEARANCE) ||
            sim_new_pc_bound(s, w, object, &x, &y)) return -1;
        if (sim_path_check(s, w, x, y)) {
            o->floor_z = (original_level + 1) * SIM_TILE_UNIT - body - SIM_TOP_CLEARANCE - 1;
            if (o->floor_z < floor) o->floor_z = floor;
            if (cap && o->floor_z < cap) o->floor_z = cap;
            x = before_x; y = before_y;
            if (collision_level(s, w, o->floor_z) ||
                sim_new_pc_bound(s, w, object, &x, &y) ||
                collision_level(s, w, o->floor_z + body - SIM_HEAD_CLEARANCE) ||
                sim_new_pc_bound(s, w, object, &x, &y)) return -1;
        }
        if (cap && o->floor_z > cap) { o->floor_z = cap; x = o->x; y = o->y; }
    } else {
        if (sim_coll_flat(w, object, x, y)) return -1;
        o->floor_z += vertical;
        if (s->support_object && !s->no_openings && !airborne) {
            const world_object *b = &w->objects[s->support_object];
            p->unk_0080 = (p->unk_007C ? p->unk_0080 : o->floor_z) + 0x800;
            if (p->unk_0080 > b->z + b->height) p->unk_0080 = b->z + b->height;
            p->unk_007C = s->support_object;
            o->floor_z = p->unk_0080;
        } else p->unk_007C = 0;
        if (old_z - SIM_QUARTER_TILE > o->floor_z)
            o->floor_z = old_z - SIM_QUARTER_TILE * s->frame_ticks / 2;
        if (old_z > o->floor_z) {
            s->collision_mode = 0;
            if (collision_level(s, w, o->floor_z)) return -1;
            o->x = x; o->y = y;
            if (sim_clear_object(s, w, object)) return -1;
            x = o->x; y = o->y;
        }
    }
    o->x = x; o->y = y;
    s->no_openings = s->unk_130d18 = 0;
    return 0;
}

/* Xox::JumbleBound 046B00: move only if the destination has the same
 * supporting floor as the source; preserve the original vertical position. */
int sim_jumble_bound(sim_state *s, world *w, unsigned object,
                     int32_t dx, int32_t dy, int collide)
{
    world_object *o;
    int32_t z, floor, x, y;
    int kind = 0;
    if (object >= w->object_count) return cm_fail("JumbleBound", object, "invalid object");
    o = &w->objects[object];
    if (s->ai && o->person >= 0 && (unsigned)o->person < w->person_count)
        kind = ai_person_kind(s->ai, w, (unsigned)o->person);
    s->save_pc_vector = o->heading;
    s->unk_130d18 = 0;
    s->no_openings = kind == 17 || kind == 5;
    s->collision_mode = !collide;
    if (collision_level(s, w, o->floor_z)) return -1;
    world_omap_delete(w, object);
    z = o->floor_z;
    if (sim_coll_flat(w, object, o->x, o->y)) return -1;
    floor = o->floor_z;
    o->floor_z = z;
    x = o->x + dx;
    y = o->y + dy;
    if (sim_path_check(s, w, o->x, o->y)) {
        x = snap_axis(o->x);
        y = snap_axis(o->y);
    }
    if (sim_new_pc_bound(s, w, object, &x, &y) || sim_coll_flat(w, object, x, y)) return -1;
    if (floor == o->floor_z) { o->x = x; o->y = y; }
    o->floor_z = z;
    world_omap_insert(w, object);
    s->no_openings = 0;
    return 0;
}
