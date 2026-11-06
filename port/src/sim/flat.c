#include "sim.h"

#define TILE_UNIT 65536
#define HEAD_CLEARANCE 0x4800
#define TOP_CLEARANCE 0x1100
#define BODY_MIN 0x5800
#define FLAT_FLOOR_FLAG 0x00040000u
#define FLAT_CEILING_FLAG 0x00800000u

static int tile_at(int32_t x, int32_t y)
{
    int32_t tx = x >> 16, ty = y >> 16;
    if (tx < 0 || tx >= CM_MAP_SIDE || ty < 0 || ty >= CM_MAP_SIDE) return -1;
    return (int)(ty * CM_MAP_SIDE + tx);
}

static int level_valid(const world *w, int level)
{
    return level >= 0 && level < CM_MAP_LEVELS && level <= w->top_level;
}

static int contains(const world_object *f, int32_t x, int32_t y)
{
    return (int64_t)x >= (int64_t)f->x - f->radius &&
           (int64_t)x <= (int64_t)f->x + f->radius &&
           (int64_t)y >= (int64_t)f->y - f->height &&
           (int64_t)y <= (int64_t)f->y + f->height;
}

/* CM_HI.EXE: Xox::PathCheck 044EF4. Signed SAR then signed remainder,
 * unlike the quarter-cell division used by MovePerson's correction. */
int sim_path_check(const sim_state *s, const world *w, int32_t x, int32_t y)
{
    int tile = tile_at(x, y), level = s->collision_level;
    unsigned shift, region;
    uint16_t mask;
    const world_aperture *a;
    if (tile < 0 || !level_valid(w, level)) return 1;
    shift = (unsigned)(((x >> 14) % 4) + ((y >> 14) % 4) * 4) & 31;
    mask = s->collision_mode ? w->inertia_mask[level][tile] : w->path_mask[level][tile];
    if (!(mask & (1u << shift))) return 0;
    if (s->no_openings) return 1;
    region = w->cells[level][tile].region;
    if (region >= SIM_APERTURE_MAX) return 1;
    a = &w->apertures[region];
    return !(a->enabled && x >= a->min_x && x <= a->max_x &&
             y >= a->min_y && y <= a->max_y);
}

static int floor_level(const world *w, int tile, int32_t head)
{
    int level = head >> 16;
    if (!level_valid(w, level)) return -1;
    while (level && !w->cells[level][tile].floor) --level;
    return level;
}

/* Xox::CollFlat 041B30: tile zero has no flat-chain traversal. */
int sim_coll_flat(world *w, unsigned object, int32_t x, int32_t y)
{
    world_object *o;
    int tile = tile_at(x, y), level;
    int32_t head;
    unsigned node, count = 0;
    if (object >= w->object_count || tile < 0)
        return cm_fail("CollFlat", object, "invalid object or tile");
    o = &w->objects[object];
    head = o->floor_z + HEAD_CLEARANCE;
    level = floor_level(w, tile, head);
    if (level < 0) return cm_fail("CollFlat", object, "invalid floor level");
    o->floor_z = level * TILE_UNIT;
    for (node = (unsigned)tile; node; node = w->flatmap.nodes[node].next) {
        unsigned id;
        const world_object *f;
        if (node >= CM_CHAIN_NODES || ++count > CM_CHAIN_NODES)
            return cm_fail("FLATMap", node, "invalid flat chain");
        id = w->flatmap.nodes[node].object;
        if (!id) continue;
        if (id >= w->object_count) return cm_fail("FLATMap", node, "invalid flat object");
        f = &w->objects[id];
        if ((f->flags & FLAT_FLOOR_FLAG) && contains(f, x, y) &&
            f->z > o->floor_z && f->z < head) o->floor_z = f->z;
    }
    return 0;
}

/* Xox::CollTopFlat 041978: strict old-height test, inclusive new-height
 * test, then one world-unit clearance. Return 1 hit, 0 clear, -1 invalid. */
int sim_coll_top_flat(sim_state *s, world *w, unsigned object,
                      int32_t old_z, int32_t height)
{
    world_object *o;
    int tile, level, hit = 0;
    int32_t ceiling;
    unsigned node, count = 0;
    if (object >= w->object_count) return cm_fail("CollTopFlat", object, "invalid object");
    o = &w->objects[object];
    tile = tile_at(o->x, o->y);
    level = old_z >> 16;
    if (tile < 0 || !level_valid(w, level)) return cm_fail("CollTopFlat", object, "invalid tile or level");
    height += object == w->player_object ? s->body_height : o->height * 2;
    if (height < BODY_MIN) height = BODY_MIN;
    while (level < w->top_level && !w->cells[level][tile].ceiling) ++level;
    ceiling = (level + 1) * TILE_UNIT;
    for (node = (unsigned)tile; node; node = w->flatmap.nodes[node].next) {
        unsigned id;
        const world_object *f;
        if (node >= CM_CHAIN_NODES || ++count > CM_CHAIN_NODES)
            return cm_fail("FLATMap", node, "invalid flat chain");
        id = w->flatmap.nodes[node].object;
        if (!id) continue;
        if (id >= w->object_count) return cm_fail("FLATMap", node, "invalid flat object");
        f = &w->objects[id];
        if ((f->flags & FLAT_CEILING_FLAG) && contains(f, o->x, o->y) &&
            old_z < f->z && f->z - height <= o->floor_z) {
            o->floor_z = f->z - height - 1;
            hit = 1;
        }
    }
    if (old_z < ceiling && ceiling - height <= o->floor_z) {
        o->floor_z = ceiling - height - 1;
        hit = 1;
    }
    return hit;
}

/* Xox::CollSwimFlat 041C4C. Equal-height flats override tile water. */
int sim_coll_swim_flat(const world *w, unsigned object, int32_t panel, int32_t art)
{
    const world_object *o;
    int tile, level, water;
    int32_t head, floor;
    unsigned node, flat = 0, count = 0;
    if (object >= w->object_count) return cm_fail("CollSwimFlat", object, "invalid object");
    o = &w->objects[object];
    tile = tile_at(o->x, o->y);
    head = o->floor_z + HEAD_CLEARANCE;
    if (tile < 0 || (level = floor_level(w, tile, head)) < 0)
        return cm_fail("CollSwimFlat", object, "invalid tile or level");
    floor = level * TILE_UNIT;
    water = w->cells[level][tile].floor == panel;
    for (node = (unsigned)tile; node; node = w->flatmap.nodes[node].next) {
        unsigned id;
        const world_object *f;
        if (node >= CM_CHAIN_NODES || ++count > CM_CHAIN_NODES)
            return cm_fail("FLATMap", node, "invalid flat chain");
        id = w->flatmap.nodes[node].object;
        if (!id) continue;
        if (id >= w->object_count) return cm_fail("FLATMap", node, "invalid flat object");
        f = &w->objects[id];
        if ((f->flags & FLAT_FLOOR_FLAG) && contains(f, o->x, o->y) &&
            f->z >= floor && f->z < head) { floor = f->z; flat = id; }
    }
    if (floor + 0x1000 < o->floor_z) water = 0;
    else if (flat) water = w->objects[flat].texture == art;
    return water;
}

/* Xox::CrouchTopFlat 0417C4: computes the body-height adjustment, does
 * not move the object and does not take the minimum over overlapping flats. */
int32_t sim_crouch_top_flat(const sim_state *s, const world *w, unsigned object,
                           int32_t adjustment, int32_t x, int32_t y)
{
    const world_object *o = &w->objects[object];
    int tile = tile_at(x, y), level = (o->floor_z + HEAD_CLEARANCE) >> 16;
    int32_t head = o->floor_z + s->body_height;
    int32_t base = o->floor_z + o->z + o->z / 2 + 0x1000;
    unsigned node, count = 0;
    if (tile < 0 || !level_valid(w, level)) return adjustment;
    while (level < w->top_level && !w->cells[level][tile].ceiling) ++level;
    for (node = (unsigned)tile; node && count++ < CM_CHAIN_NODES;
         node = w->flatmap.nodes[node].next) {
        unsigned id;
        const world_object *f;
        if (node >= CM_CHAIN_NODES) break;
        id = w->flatmap.nodes[node].object;
        if (!id || id >= w->object_count) continue;
        f = &w->objects[id];
        if ((f->flags & FLAT_CEILING_FLAG) && contains(f, x, y) &&
            o->floor_z + HEAD_CLEARANCE < f->z && f->z - TOP_CLEARANCE < head)
            adjustment = f->z - TOP_CLEARANCE - base;
    }
    if ((level + 1) * TILE_UNIT - TOP_CLEARANCE < head)
        adjustment = (level + 1) * TILE_UNIT - TOP_CLEARANCE - base;
    return adjustment;
}
