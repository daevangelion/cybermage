#include "world_internal.h"

#define RAY_MAX_COORD 0x1FC000
#define RAY_STEPS_MAX 128

static unsigned tile_of(int32_t x, int32_t y)
{
    return ((uint32_t)y >> 16) * 32u | (((uint32_t)x >> 16) & 31u);
}

/* TileSight 0x5D5A8. Downward sight deliberately tests only the starting
 * floor, even across multiple levels (original0x5D63B). */
int world_tile_sight(const world *w, unsigned tile, int from, int to,
                      unsigned side, unsigned target, int target_level)
{
    unsigned wall;
    uint32_t flags;
    int level;
    if (tile >= CM_MAP_CELLS || from < 0 || from >= CM_MAP_LEVELS ||
        to < 0 || to >= CM_MAP_LEVELS) return -1;
    if (from < to) {
        for (level = from; level < to; ++level)
            if (w->cells[level][tile].ceiling) return -1;
    } else if (from > to && w->cells[from][tile].floor) return -1;
    if (tile == target && to == target_level) return 1;
    if (w->cells[0][tile].wall_sides == UINT32_MAX) return -1;
    wall = (w->cells[to][tile].wall_sides >> ((side * 8) & 31)) & 255u;
    if (!wall) return 0;
    flags = w->map_id_flags[wall];
    return (flags & 0x40000000u) && !(flags & 0x10000000u) ? 0 : -1;
}

static int missile_sight(const world *w, unsigned tile, int level,
                         unsigned side, unsigned target)
{
    unsigned wall;
    uint32_t flags;
    if (tile == target) return 1;
    if (tile >= CM_MAP_CELLS || level < 0 || level > w->top_level ||
        w->cells[0][tile].wall_sides == UINT32_MAX) return -1;
    wall = (w->cells[level][tile].wall_sides >> (side * 8)) & 255u;
    if (!wall) return 0;
    flags = w->map_id_flags[wall];
    /* MissileSight 0x5E59C tests high-byte40 AND second-high-byte10. */
    return (flags & 0x40000000u) && (flags & 0x00100000u) ? 0 : -1;
}

static int32_t clamp_coord(int32_t v) { return v < 0 ? 0 : v > RAY_MAX_COORD ? RAY_MAX_COORD : v; }

/* RaySight/RayMissile and RayCast share crossings, not a Euclidean sampling
 * approximation. Asymmetric ties are retained for all four quadrants.
 * [rust: native/image1/raycast.rs], original0x5D6D4/0x5E5C0 checked. */
static int trace(world *w, int32_t x, int32_t y, int32_t z,
                  int32_t tx, int32_t ty, int32_t tz, uint16_t angle,
                  int32_t range, int missile, int32_t *hit)
{
    unsigned quadrant = angle >> 14, q = (angle & 0x3FFFu) >> 3;
    int32_t cu = (x & ~255) + 128, cv = (y & ~255) + 128;
    int32_t left = cu & 0xFFFF, bottom = cv & 0xFFFF;
    int32_t right = 0xFFFF - left, top = 0xFFFF - bottom;
    int32_t dy = w->trig.tan[quadrant & 1 ? 2047 - q : q];
    int32_t dx = w->trig.tan[quadrant & 1 ? q : 2047 - q];
    int sx = quadrant == 0 || quadrant == 3 ? 1 : -1;
    int sy = quadrant < 2 ? 1 : -1;
    int32_t frac_x = sx > 0 ? right : left;
    int32_t frac_y = sy > 0 ? top : bottom;
    int32_t x1 = cu + sx * world_mul16(frac_y, dx);
    int32_t y1 = cv + sy * frac_y;
    int32_t x2 = cu + sx * frac_x;
    int32_t y2 = cv + sy * world_mul16(frac_x, dy);
    int first = sx > 0 ? x1 <= x2 : x1 >= x2;
    int32_t previous_z = z;
    unsigned target = tile_of(tx, ty), step;
    if (range <= 0) return cm_fail("world/ray", 0, "nonpositive ray distance");
    for (step = 0; step < RAY_STEPS_MAX; ++step) {
        int32_t rx = first ? x1 : x2, ry = first ? y1 : y2;
        unsigned side = first ? (sy > 0 ? 1u : 3u) : (sx > 0 ? 0u : 2u);
        unsigned tile = tile_of(rx, ry);
        int32_t distance = cm_distance(x, y, rx, ry), next_z;
        int64_t vertical;
        int result;
        if (distance > range) distance = range;
        vertical = (int64_t)distance * ((int64_t)tz - z) / range;
        if (vertical + z < INT32_MIN || vertical + z > INT32_MAX)
            return cm_fail("world/ray", step, "vertical interpolation overflow");
        next_z = (int32_t)(z + vertical);
        if (missile) result = missile_sight(w, tile, next_z >> 16, side, target);
        else result = world_tile_sight(w, tile, previous_z >> 16, next_z >> 16,
                                      side, target, tz >> 16);
        if (result) {
            if (hit) { hit[0] = rx; hit[1] = ry; hit[2] = next_z; hit[3] = distance; }
            return result;
        }
        previous_z = tz < z ? (next_z > tz ? next_z : tz) : (next_z < tz ? next_z : tz);
        if (first) {
            x1 = clamp_coord(x1 + sx * dx);
            y1 = clamp_coord(y1 + sy * 0x10000);
            first = sx > 0 ? x1 <= x2 : x1 >= x2;
        } else {
            x2 = clamp_coord(x2 + sx * 0x10000);
            y2 = clamp_coord(y2 + sy * dy);
            first = sy > 0 ? y2 > y1 : y2 < y1;
        }
    }
    return cm_fail("world/ray", RAY_STEPS_MAX, "ray failed to terminate within map crossings");
}

int world_ray_sight(world *w, unsigned source, unsigned target, int32_t distance,
                    int32_t z, int32_t tz, uint16_t angle)
{
    if (source >= w->object_count || target >= w->object_count)
        return cm_fail("world/ray", source, "invalid sight object");
    return trace(w, w->objects[source].x, w->objects[source].y, z,
                  w->objects[target].x, w->objects[target].y, tz,
                  angle, distance, 0, 0);
}
int world_ray_missile_xyz(world *w, int32_t x, int32_t y, int32_t z,
                          int32_t tx, int32_t ty, int32_t tz, uint16_t angle,
                          int32_t distance, int32_t *hx, int32_t *hy,
                          int32_t *hz, int32_t *hd)
{
    int32_t hit[4] = {x,y,z,0};
    int result;
    if (!hx || !hy || !hz || !hd)
        return cm_fail("world/ray", 0, "missing missile outputs");
    result = trace(w,x,y,z,tx,ty,tz,angle,distance,1,hit);
    *hx = hit[0]; *hy = hit[1]; *hz = hit[2]; *hd = hit[3];
    return result;
}
int world_ray_missile(world *w, unsigned source, unsigned target,
                      int32_t distance, int32_t z, int32_t tz,
                      uint16_t angle, int32_t *hx, int32_t *hy,
                      int32_t *hz, int32_t *hd)
{
    if (source >= w->object_count || target >= w->object_count)
        return cm_fail("world/ray", source, "invalid missile object");
    return world_ray_missile_xyz(w,w->objects[source].x,w->objects[source].y,z,
                                 w->objects[target].x,w->objects[target].y,tz,
                                 angle,distance,hx,hy,hz,hd);
}
