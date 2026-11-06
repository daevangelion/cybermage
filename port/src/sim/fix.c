#include "sim_internal.h"

#define FX_PLATFORM_FLAG 0x00200000u
#define FX_PLATFORM_SLOP 0x800
#define FX_SUPPORT_HEADING 0xc00

static int inside(int32_t x, int32_t y, int32_t left, int32_t bottom,
                   int32_t width, int32_t height)
{
    return x > left && x < left + width && y > bottom && y < bottom + height;
}

static int aperture_contains(const sim_state *s, const world *w, int32_t x, int32_t y)
{
    int tx = x >> 16, ty = y >> 16, level = s->collision_level;
    unsigned region;
    const world_aperture *a;
    if (tx < 0 || tx >= CM_MAP_SIDE || ty < 0 || ty >= CM_MAP_SIDE ||
        level < 0 || level >= CM_MAP_LEVELS) return 0;
    region = w->cells[level][ty * CM_MAP_SIDE + tx].region;
    if (region >= SIM_APERTURE_MAX) return 0;
    a = &w->apertures[region];
    return a->enabled && x >= a->min_x && x <= a->max_x &&
           y >= a->min_y && y <= a->max_y;
}

static int candidate(const sim_state *s, const world *w, const world_object *o,
                      const world_object *f, int32_t x, int32_t y,
                      int32_t left, int32_t bottom, int32_t width, int32_t height,
                      int32_t *out_x, int32_t *out_y)
{
    if (inside(x, y, left, bottom, width, height)) return 0;
    cm_fixed_rotate(&w->trig, &x, &y, o->x, o->y, f->heading);
    if (sim_path_check(s, w, x, y)) return 0;
    *out_x = x;
    *out_y = y;
    return 1;
}

/* CM_HI.EXE Xox::FixNxNy 044090..044A82. Oriented frames are reduced to
 * a rectangle around the mover's original centre, then candidates rotate back.
 * The reference's extended doorway depth is deliberately not applied. */
int sim_fix_nx_ny(sim_state *s, const world *w, unsigned object, unsigned other,
                  int32_t *x, int32_t *y)
{
    const world_object *o = &w->objects[object], *f = &w->objects[other];
    int32_t body, clearance, fx = f->x, fy = f->y, nx = *x, ny = *y;
    int32_t left, bottom, width = SIM_ANGLE_HALF, height, cx, cy, dx = 0, dy = 0, cross;
    uint16_t vector = object == w->player_object ? s->save_pc_vector : o->heading;
    uint16_t angle = cm_atan64k(o->x, o->y, nx, ny), local_angle;
    int opening = aperture_contains(s, w, nx, ny);
    int platform = (f->flags & FX_PLATFORM_FLAG) != 0;
    unsigned order;
    if (opening && !platform) return 0;
    if (object == w->player_object) {
        body = s->body_height;
        clearance = SIM_HEAD_CLEARANCE + s->unk_144b34;
        if (clearance < 0x1000) clearance = 0x1000;
    } else {
        body = f == o ? 0 : o->height + 0x1000;
        if (body < SIM_BODY_MIN) body = SIM_BODY_MIN;
        clearance = SIM_HEAD_CLEARANCE;
    }
    if ((f->z - f->height > o->floor_z + body ||
         f->z + f->height < o->floor_z + clearance) && !platform) return 0;
    cm_fixed_rotate(&w->trig, &fx, &fy, o->x, o->y, (uint16_t)-f->heading);
    cm_fixed_rotate(&w->trig, &nx, &ny, o->x, o->y, (uint16_t)-f->heading);
    left = fx - SIM_QUARTER_TILE;
    bottom = fy - f->radius - SIM_QUARTER_TILE;
    height = f->radius * 2 + SIM_ANGLE_HALF;
    if (platform) {
        int32_t narrow_bottom = fy - f->radius - FX_PLATFORM_SLOP;
        if (nx < left - 0x200 || nx > left + 0x8200 ||
            ny < narrow_bottom || ny > narrow_bottom + f->radius * 2 + 0x1000) return 0;
        if ((int16_t)(vector - f->heading) > -FX_SUPPORT_HEADING &&
            (int16_t)(vector - f->heading) < FX_SUPPORT_HEADING &&
            o->floor_z + s->body_height > f->z - f->height &&
            f->z + f->height >= o->floor_z &&
            (!s->support_object || w->objects[s->support_object].z < f->z))
            s->support_object = (int32_t)other;
        clearance = object == w->player_object ? SIM_ANGLE_EIGHTH + s->unk_144b34 : SIM_ANGLE_EIGHTH;
        if (clearance < 0x1000) clearance = 0x1000;
        if (opening || f->z - f->height > o->floor_z + body ||
            f->z + f->height < o->floor_z + clearance) return 0;
    } else if (!inside(nx, ny, left, bottom, width, height)) return 0;
    s->collision_flags |= 1;
    cx = o->x;
    cy = o->y;
    if (inside(cx, cy, left, bottom, width, height)) {
        int32_t near_x = cx < left + width / 2 ? left : left + width;
        int32_t near_y = cy < bottom + height / 2 ? bottom : bottom + height;
        int64_t x_distance = (int64_t)near_x - cx, y_distance = (int64_t)near_y - cy;
        if (x_distance < 0) x_distance = -x_distance;
        if (y_distance < 0) y_distance = -y_distance;
        if (x_distance < y_distance) cx = near_x;
        else cy = near_y;
    }
    if (o->x < left) dx = left - 1 - o->x;
    else if (o->x > left + width) dx = left + width + 1 - o->x;
    local_angle = (uint16_t)(angle - f->heading);
    if (dx) {
        cross = o->y + sim_mul16(dx < 0 ? -dx : dx, sim_slope(w, local_angle));
        if (cross >= bottom && cross < bottom + height) cx = o->x + dx;
    }
    if (o->y < bottom) dy = bottom - 1 - o->y;
    else if (o->y > bottom + height) dy = bottom + height + 1 - o->y;
    if (dy) {
        cross = o->x + sim_mul16(dy < 0 ? -dy : dy,
                                 -sim_slope(w, (uint16_t)(local_angle - SIM_ANGLE_QUARTER)));
        if (cross >= left && cross < left + width) cy = o->y + dy;
    }
    order = ((uint16_t)(angle + SIM_ANGLE_EIGHTH) / SIM_ANGLE_QUARTER) & 1;
    if (!order) {
        if (candidate(s, w, o, f, nx, cy, left, bottom, width, height, x, y)) return 0;
        if (candidate(s, w, o, f, cx, ny, left, bottom, width, height, x, y)) return 0;
    } else {
        if (candidate(s, w, o, f, cx, ny, left, bottom, width, height, x, y)) return 0;
        if (candidate(s, w, o, f, nx, cy, left, bottom, width, height, x, y)) return 0;
    }
    cm_fixed_rotate(&w->trig, &cx, &cy, o->x, o->y, f->heading);
    if (sim_path_check(s, w, cx, cy)) { *x = o->x; *y = o->y; }
    else { *x = cx; *y = cy; }
    return 0;
}
