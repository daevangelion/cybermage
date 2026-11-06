#include "sim_internal.h"
#include "ai.h"
#include <limits.h>

#define FIRE_PANEL_FLAG 0x08000000u
#define FIRE_PANEL_PASS 0x00100000u
#define FIRE_PANEL_CONTACT 0x200
#define FIRE_WALL_CONTACT 0x800

/* CollFire's IDIV helper returns INT_MAX for zero divisor (0402CB etc.). */
static int32_t scale(int32_t value, int32_t numerator, int32_t denominator)
{
    int64_t n;
    if (!denominator) return INT32_MAX;
    n = (int64_t)value * numerator / denominator;
    return n < INT32_MIN || n > INT32_MAX ? INT32_MIN : (int32_t)n;
}

static int32_t absolute(int32_t n)
{
    return n < 0 ? -n : n;
}

/* 0407AD..0408BD and 0414AA..0415BA. These are deliberately tile-snapped
 * aperture bounds with scene-specific clearance, not PathCheck's bounds. */
static int opening(const world *w, int32_t x, int32_t y, int32_t z)
{
    const world_aperture *a;
    unsigned region;
    int level = z >> 16, tx = x >> 16, ty = y >> 16;
    int wide;
    uint32_t low, high;
    if (level < 0 || level >= CM_MAP_LEVELS || tx < 0 || tx >= CM_MAP_SIDE ||
        ty < 0 || ty >= CM_MAP_SIDE) return 0;
    region = w->cells[level][tx + ty * CM_MAP_SIDE].region;
    if (region >= SIM_APERTURE_MAX) return 0;
    a = &w->apertures[region];
    if (!a->enabled) return 0;
    wide = (w->scene == 1 && region == 7) || (w->scene == 2 &&
        (region == 73 || region == 74 || region == 75 || region == 77 ||
         region == 28 || region == 46 || region == 48 || region == 54));
    low = ((uint32_t)a->min_x & 0xffff0000u) + (wide ? 0x1800u : 0x4000u);
    high = ((uint32_t)a->max_x & 0xffff0000u) + (wide ? 0xe800u : 0xc000u);
    if ((uint32_t)x < low || (uint32_t)x > high) return 0;
    low = ((uint32_t)a->min_y & 0xffff0000u) + (wide ? 0x1800u : 0x4000u);
    high = ((uint32_t)a->max_y & 0xffff0000u) + (wide ? 0xe800u : 0xc000u);
    return (uint32_t)y >= low && (uint32_t)y <= high;
}

static int vertical_hit(const world_object *f, int panel, int32_t z)
{
    int32_t top = sim_add32(f->z, panel ? 0 : f->floor_z);
    return sim_sub32(top, f->unk_6F220) > z && sim_sub32(top, f->unk_71E88) < z;
}

/* 040050..0417C0. Work is bounded by MakeMissileList's original object cap.
 * Sweeps test object planes first, then trace map crossings only as far as
 * the nearest object/flat contact. No endpoint-only collision shortcut. */
int sim_coll_fire(sim_state *s, world *w, unsigned missile)
{
    ai_state *a = s->ai;
    ai_missile *m;
    world_object *o;
    int32_t ox, oy, oz, top, dz, nz, distance, nearest, cx = 0, cy = 0;
    unsigned angle, i;
    int result = 0, clipped;
    if (!a || missile >= AI_MISSILE_MAX) {
        cm_fail("CollFire", missile, "invalid missile index"); return SIM_COLL_ERROR;
    }
    m = &a->missiles[missile];
    if (m->object < 0 || (unsigned)m->object >= w->object_count || a->missile_count > CM_OBJECT_MAX) {
        cm_fail("CollFire", missile, "invalid missile object or candidate count"); return SIM_COLL_ERROR;
    }
    o = &w->objects[m->object];
    ox = o->x; oy = o->y; oz = o->floor_z; top = sim_add32(oz, o->z);
    angle = o->heading;
    distance = scale(m->speed, s->frame_ticks, 2);
    dz = scale(m->vertical, s->frame_ticks, 2);
    if (distance < 0 || dz == INT32_MIN) {
        cm_fail("CollFire", missile, "missile interpolation overflow"); return SIM_COLL_ERROR;
    }
    if (!(m->type == 38 || (m->type == 1 && m->ticks))) dz = -dz;
    nz = sim_add32(oz, dz);
    nearest = distance;
    o->floor_z = nz;
    clipped = sim_coll_fire_flat(w, (unsigned)m->object, oz);
    if (clipped == -2) return SIM_COLL_ERROR;
    if (clipped) {
        int32_t travel = scale(distance, sim_sub32(o->floor_z, oz), dz);
        int32_t saved_x = o->x, saved_y = o->y, old_clip = o->floor_z;
        if (travel == INT32_MIN) return SIM_COLL_ERROR;
        o->x = sim_add32(o->x, sim_mul16(travel, w->trig.cos[angle >> 5]));
        o->y = sim_add32(o->y, sim_mul16(travel, w->trig.sin[angle >> 5]));
        o->floor_z = nz;
        clipped = sim_coll_fire_flat(w, (unsigned)m->object, old_clip);
        if (clipped == -2) return SIM_COLL_ERROR;
        result = clipped;
        if (clipped) {
            if (m->type == 46) result = 0;
            else {
                nearest = scale(distance, sim_sub32(o->floor_z, oz), dz);
                cx = sim_add32(saved_x, sim_mul16(nearest, w->trig.cos[angle >> 5]));
                cy = sim_add32(saved_y, sim_mul16(nearest, w->trig.sin[angle >> 5]));
            }
            nz = o->floor_z;
        }
        o->x = saved_x; o->y = saved_y;
    }
    o->floor_z = oz;
    for (i = 0; i < a->missile_count; ++i) {
        unsigned id = a->missile_objects[i];
        const world_object *f;
        if (id >= w->object_count) {
            cm_fail("CollFire", id, "invalid candidate object"); return SIM_COLL_ERROR;
        }
        if ((int32_t)id == m->source) continue;
        f = &w->objects[id];
        if (f->flags & FIRE_PANEL_FLAG) {
            int32_t fx = f->x, fy = f->y, dx, ly, travel, px, py, z;
            uint16_t local_angle = (uint16_t)(angle - f->heading);
            int toward_positive_x = local_angle < 0x4000 || local_angle >= 0xc000;
            if (f->flags & FIRE_PANEL_PASS) continue;
            cm_fixed_rotate(&w->trig, &fx, &fy, ox, oy, (uint16_t)-f->heading);
            if ((toward_positive_x && ox > fx) || (!toward_positive_x && ox < fx)) continue;
            dx = fx - ox;
            ly = sim_mul16(absolute(dx), sim_slope(w, local_angle));
            if (absolute(oy + ly - fy) >= f->radius) continue;
            travel = sim_mul16(dx, w->trig.cos[local_angle >> 5]) +
                     sim_mul16(ly, w->trig.sin[local_angle >> 5]);
            if (travel <= 0 || travel >= nearest) continue;
            z = sim_add32(top, scale(dz, travel, distance));
            if (!vertical_hit(f, 1, z)) continue;
            px = fx + (ox > fx ? FIRE_PANEL_CONTACT : -FIRE_PANEL_CONTACT);
            py = oy + ly;
            cm_fixed_rotate(&w->trig, &px, &py, ox, oy, f->heading);
            if (opening(w, px, py, f->z)) continue;
            nearest = travel; cx = px; cy = py; result = (int)id;
        } else {
            int32_t radius = f->radius / 4 + f->radius / 8;
            int32_t left = f->x - radius, right = f->x + radius;
            int32_t bottom = f->y - radius, upper = f->y + radius;
            int positive_x = angle < 0x4000 || angle >= 0xc000;
            int positive_y = angle < 0x8000;
            int axis;
            if ((positive_x && ox > right) || (!positive_x && ox < left) ||
                (positive_y && oy > upper) || (!positive_y && oy < bottom)) continue;
            if (ox >= left && ox <= right && oy >= bottom && oy <= upper) {
                if (vertical_hit(f, 0, top)) { cx = ox; cy = oy; nearest = 0; result = (int)id; }
                continue;
            }
            for (axis = 0; axis < 2; ++axis) {
                int32_t px = ox, py = oy, travel, z;
                if (!axis) {
                    int32_t slope, offset;
                    if ((positive_x && ox >= left) || (!positive_x && ox <= right)) continue;
                    px = positive_x ? left : right;
                    slope = sim_slope(w, (uint16_t)angle);
                    offset = sim_mul16(absolute(px - ox), absolute(slope));
                    py += slope < 0 ? -offset : offset;
                    if (py <= bottom || py >= upper) continue;
                } else {
                    int32_t slope, offset;
                    if ((positive_y && oy >= bottom) || (!positive_y && oy <= upper)) continue;
                    py = positive_y ? bottom : upper;
                    slope = -sim_slope(w, (uint16_t)(angle - SIM_ANGLE_QUARTER));
                    offset = sim_mul16(absolute(py - oy), absolute(slope));
                    px += slope < 0 ? -offset : offset;
                    if (px <= left || px >= right) continue;
                }
                travel = cm_distance(ox, oy, px, py);
                if (travel >= nearest) continue;
                z = sim_add32(top, scale(dz, travel, distance));
                if (!vertical_hit(f, 0, z)) continue;
                cx = px; cy = py; nearest = travel; result = (int)id;
            }
        }
    }
    if (nearest > 0) {
        int32_t hx, hy, hz, hd;
        int32_t tx = sim_add32(ox, sim_mul16(nearest, w->trig.cos[angle >> 5]));
        int32_t ty = sim_add32(oy, sim_mul16(nearest, w->trig.sin[angle >> 5]));
        int32_t tz = sim_add32(oz, scale(dz, nearest, distance));
        int hit = world_ray_missile_xyz(w, ox, oy, oz, tx, ty, tz, (uint16_t)angle,
                                         nearest, &hx, &hy, &hz, &hd);
        if (hit == -1) {
            if (hz < 0) hz = 0;
            else if (hz > w->unk_130D20) hz = w->unk_130D20;
            else if (opening(w, hx, hy, hz)) hit = 0;
            if (hit) {
                unsigned backward = (uint16_t)(angle + SIM_ANGLE_HALF) >> 5;
                o->x = sim_add32(hx, sim_mul16(FIRE_WALL_CONTACT, w->trig.cos[backward]));
                o->y = sim_add32(hy, sim_mul16(FIRE_WALL_CONTACT, w->trig.sin[backward]));
                o->floor_z = hz; m->target = -1; return -1;
            }
        }
    }
    if (result < 0) {
        o->x = cx; o->y = cy; o->floor_z = nz; m->target = -1;
        return -1;
    }
    if (result > 0) {
        const world_object *target = &w->objects[result];
        o->x = cx; o->y = cy;
        m->target = result;
        m->ticks = cm_distance(target->x, target->y, cx, cy);
        o->heading = cm_atan64k(target->x, target->y, cx, cy);
        o->floor_z = sim_add32(oz, scale(dz, nearest, distance));
        return result;
    }
    o->x = sim_add32(ox, sim_mul16(nearest, w->trig.cos[angle >> 5]));
    o->y = sim_add32(oy, sim_mul16(nearest, w->trig.sin[angle >> 5]));
    o->floor_z = nz;
    return 0;
}
