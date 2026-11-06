#include "sim_internal.h"

#define FIRE_FLAT_MARGIN 0x400

/* CM_HI.EXE Xox::CollFireFlat 03FDE0..04003C: old_z is the previous
 * projectile floor_z, not a panel or texture argument. Returns original -1
 * on clipping, 0 clear; malformed scene state returns the separate -2. */
int sim_coll_fire_flat(world *w, unsigned object, int32_t old_z)
{
    world_object *o;
    int tx, ty, level, floor_level, ceiling_level, result = 0;
    int32_t floor, ceiling;
    unsigned node, count = 0;
    if (object >= w->object_count) {
        cm_fail("CollFireFlat", object, "invalid object");
        return -2;
    }
    o = &w->objects[object];
    tx = o->x >> 16; ty = o->y >> 16; level = old_z >> 16;
    if (tx < 0 || tx >= CM_MAP_SIDE || ty < 0 || ty >= CM_MAP_SIDE ||
        level < 0 || level > w->top_level || level >= CM_MAP_LEVELS) {
        cm_fail("CollFireFlat", object, "invalid tile or level");
        return -2;
    }
    node = (unsigned)(tx + ty * CM_MAP_SIDE);
    ceiling_level = floor_level = level;
    while (ceiling_level < w->top_level && !w->cells[ceiling_level][node].ceiling) ++ceiling_level;
    while (floor_level && !w->cells[floor_level][node].floor) --floor_level;
    ceiling = ceiling_level * SIM_TILE_UNIT + 0xffff;
    floor = floor_level * SIM_TILE_UNIT;
    while (node) {
        unsigned id;
        const world_object *f;
        if (node >= CM_CHAIN_NODES || ++count > CM_CHAIN_NODES) {
            cm_fail("FLATMap", node, "invalid flat chain");
            return -2;
        }
        id = w->flatmap.nodes[node].object;
        if (id >= w->object_count) {
            cm_fail("FLATMap", node, "invalid flat object");
            return -2;
        }
        f = &w->objects[id];
        if (id && o->x >= f->x - f->radius && o->x <= f->x + f->radius &&
            o->y >= f->y - f->height && o->y <= f->y + f->height) {
            if (old_z >= f->z && f->z + FIRE_FLAT_MARGIN >= o->floor_z) {
                o->floor_z = f->z + FIRE_FLAT_MARGIN;
                result = -1;
            } else if (old_z <= f->z && o->floor_z + o->z >= f->z - FIRE_FLAT_MARGIN) {
                o->floor_z = f->z - o->z - FIRE_FLAT_MARGIN;
                result = -1;
            }
        }
        node = w->flatmap.nodes[node].next;
    }
    if (old_z >= floor && floor + FIRE_FLAT_MARGIN >= o->floor_z) {
        o->floor_z = floor + FIRE_FLAT_MARGIN;
        result = -1;
    } else if (old_z <= ceiling && o->floor_z + o->z >= ceiling - FIRE_FLAT_MARGIN) {
        o->floor_z = ceiling - o->z - FIRE_FLAT_MARGIN;
        result = -1;
    }
    return result;
}
