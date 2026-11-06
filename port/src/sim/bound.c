#include "sim_internal.h"
#include "ai.h"
#include "events.h"

#define OBJECT_FXO 0x08000000u
#define OBJECT_PASSABLE 0x01000000u
#define COLLISION_VEHICLE_FORCE 0x7000

/* CM_HI.EXE Xox::CollObject 0435EC: visit up to four tile chains in
 * top-left, top-right, bottom-left, bottom-right order. Do not deduplicate:
 * multi-tile FX entries intentionally get revisited after each correction. */
int sim_coll_object(sim_state *s, world *w, unsigned object, int32_t *x, int32_t *y)
{
    world_object *o = &w->objects[object];
    int32_t left = *x - SIM_QUARTER_TILE, right = *x + SIM_QUARTER_TILE;
    int32_t bottom = *y - SIM_QUARTER_TILE, top = *y + SIM_QUARTER_TILE;
    int32_t low_z = o->floor_z + 0x1000;
    int32_t high_z = o->floor_z + (object == w->player_object ? s->body_height : o->z + o->height) - 0x1000;
    int tx[2] = {left >> 16, right >> 16}, ty[2] = {bottom >> 16, top >> 16};
    unsigned row, col;
    if (s->collision_observe)
        s->collision_observe(s->collision_context, w, object, *x, *y);
    if(s->events) events_pickups_begin(s->events);
    for (row = 0; row < 2; ++row) {
        if (row && ty[0] == ty[1]) continue;
        for (col = 0; col < 2; ++col) {
            unsigned node, count = 0;
            if (col && tx[0] == tx[1]) continue;
            if (tx[col] < 0 || tx[col] >= CM_MAP_SIDE || ty[row] < 0 || ty[row] >= CM_MAP_SIDE)
                return cm_fail("CollObject", object, "object outside collision map");
            node = (unsigned)(ty[row] * CM_MAP_SIDE + tx[col]);
            while (node) {
                unsigned id;
                world_object *b;
                int person;
                if (node >= CM_CHAIN_NODES || ++count > CM_CHAIN_NODES)
                    return cm_fail("OMap", node, "invalid object chain");
                id = w->omap.nodes[node].object;
                if (!id || id == object) { node = w->omap.nodes[node].next; continue; }
                if (id >= w->object_count) return cm_fail("OMap", node, "invalid object index");
                b = &w->objects[id];
                if (b->flags & OBJECT_FXO) {
                    if (sim_fix_nx_ny(s, w, object, id, x, y)) return -1;
                } else if (right > b->x - b->unk_74AF0 && left < b->x + b->unk_74AF0 &&
                           top > b->y - b->unk_74AF0 && bottom < b->y + b->unk_74AF0 &&
                           !(b->flags & OBJECT_PASSABLE) &&
                           b->floor_z + b->z + b->height > low_z && high_z > b->floor_z) {
                    person = b->person;
                    if (b->targetable == 1) {
                        if (person < 0 || (unsigned)person >= w->person_count)
                            return cm_fail("CollObject", id, "invalid PERSON");
                        if (s->unk_130d18 && s->ai && w->persons[person].status != 2) {
                            int kind = ai_person_kind(s->ai, w, (unsigned)person);
                            if (kind != 17 && kind != 5) {
                                if (s->unk_130d18 > 1 && ai_damage_monster(s->ai, w,
                                    (unsigned)person, s->unk_130d18, (int32_t)w->player_object)) return -1;
                                if (ai_blast_physics(s->ai, w, id, w->player_object,
                                                      COLLISION_VEHICLE_FORCE)) return -1;
                            }
                        }
                        if (w->persons[person].status != 2)
                            sim_obj_nx_ny(s, w, object, id, x, y);
                    } else {
                        unsigned item = 0;
                        if (b->targetable == 2) {
                            if (b->person < 0 || (unsigned)b->person >= w->prop_count ||
                                w->prop_objects[b->person] != id)
                                return cm_fail("CollObject", id, "invalid prop placement");
                            item = (uint16_t)w->prop_items[b->person];
                        }
                        if (item) {
                            if (!s->events || item > s->events->definition_count)
                                return cm_fail("CollObject", id, "invalid pickup definition");
                            if (object == w->player_object) {
                                if (events_coll_item(s->events, w, id, object)) return -1;
                            } else if (s->events->definitions[item].kind != 16) {
                                sim_obj_nx_ny(s, w, object, id, x, y);
                            }
                        } else {
                            /* 0438CB pushes Y; [esp+8] at0438CE is the
                             * candidate saved at the former [esp+4]. */
                            sim_obj_nx_ny(s, w, object, id, x, y);
                        }
                    }
                }
                node = w->omap.nodes[node].next;
            }
        }
    }
    return s->events?events_pickups_end(s->events,w):0;
}

static const world_aperture *old_aperture(const sim_state *s, const world *w,
                                         const world_object *o)
{
    int tx = o->x >> 16, ty = o->y >> 16;
    unsigned region;
    const world_aperture *a;
    if (tx < 0 || tx >= CM_MAP_SIDE || ty < 0 || ty >= CM_MAP_SIDE ||
        s->collision_level < 0 || s->collision_level >= CM_MAP_LEVELS) return NULL;
    region = w->cells[s->collision_level][ty * CM_MAP_SIDE + tx].region;
    if (region >= SIM_APERTURE_MAX) return NULL;
    a = &w->apertures[region];
    return a->enabled && o->x >= a->min_x && o->x <= a->max_x &&
           o->y >= a->min_y && o->y <= a->max_y ? a : NULL;
}

/* Xox::NewPCBound 045AB8. Original error occurs after the eleventh
 * blocked correction, even if that correction happens to reach a free cell. */
static int clear_fxo(sim_state *s, const world *w, unsigned object, int32_t *x, int32_t *y)
{
    int tx[2] = {(*x - SIM_QUARTER_TILE) >> 16, (*x + SIM_QUARTER_TILE) >> 16};
    int ty[2] = {(*y - SIM_QUARTER_TILE) >> 16, (*y + SIM_QUARTER_TILE) >> 16};
    unsigned row, col;
    for (row = 0; row < 2; ++row) {
        if (row && ty[0] == ty[1]) continue;
        for (col = 0; col < 2; ++col) {
            unsigned node, count = 0;
            if (col && tx[0] == tx[1]) continue;
            if (tx[col] < 0 || tx[col] >= CM_MAP_SIDE || ty[row] < 0 || ty[row] >= CM_MAP_SIDE)
                return cm_fail("ClearFxoObject", object, "invalid tile");
            node = (unsigned)(tx[col] + ty[row] * CM_MAP_SIDE);
            while (node) {
                unsigned id;
                if (node >= CM_CHAIN_NODES || ++count > CM_CHAIN_NODES)
                    return cm_fail("OMap", node, "invalid object chain");
                id = w->omap.nodes[node].object;
                if (id >= w->object_count) return cm_fail("OMap", node, "invalid object");
                if (id && (w->objects[id].flags & OBJECT_FXO) &&
                    sim_fix_nx_ny(s, w, object, id, x, y)) return -1;
                node = w->omap.nodes[node].next;
            }
        }
    }
    return 0;
}

/* NewMONBound 045F68..0463C3: original asymmetric corner tests and
 * heading-dependent 0800 nudges. Mask <<3 at0461A5 tests all nonzero mask
 * bits; replacing it by an 8000 test would change the original. */
static unsigned monster_radius(const sim_state *s, const world *w, unsigned object,
                                int32_t *x, int32_t *y)
{
    const world_object *o = &w->objects[object];
    int tile = (*x >> 16) + (*y >> 16) * CM_MAP_SIDE;
    int qx = (*x >> 14) % 4, qy = (*y >> 14) % 4;
    int32_t r = o->radius + 0x400, far;
    unsigned mask, count = 0, heading = o->heading;
    int west = heading >= 0x6000 && heading <= 0xa000;
    int east = heading <= 0x2000 || heading >= 0xe000;
    int south = heading >= 0xa000 && heading <= 0xe000;
    int north = heading >= 0x2000 && heading <= 0x6000;
    if (!(o->flags & 0x8000u) || tile < 0 || tile >= CM_MAP_CELLS) return 0;
    if (r > 0x7800) r = 0x7800;
    far = SIM_TILE_UNIT - r;
    mask = s->collision_mode ? w->inertia_mask[s->collision_level][tile] :
                              w->path_mask[s->collision_level][tile];
    if (qx == 1) {
        if ((mask & 0x10) && r > (*x & 0xffff)) {
            *x = (*x & ~0xffff) + r;
            if (west) ++count;
        }
        if (r > (*y & 0xffff)) {
            if (mask & 1) {
                if (west) *y += 0x800;
                else if (r > (*x & 0xffff)) *x = (*x & ~0xffff) + r;
            }
        } else if ((*y & 0xffff) > far && (mask & 0x1000)) {
            if (west) *y -= 0x800;
            else if (r > (*x & 0xffff)) *x = (*x & ~0xffff) + r;
        }
    } else if (qx == 2) {
        if (mask & 0x80) {
            if ((*x & 0xffff) > far) *x = (*x & ~0xffff) + far;
            if (east) ++count;
        }
        if (r > (*y & 0xffff)) {
            if (mask & 8) {
                if (east) *y += 0x800;
                else if ((*x & 0xffff) > far) *x = (*x & ~0xffff) + far;
            }
        } else if ((*y & 0xffff) > far && mask) {
            if (east) *y -= 0x800;
            else if ((*x & 0xffff) > far) *x = (*x & ~0xffff) + far;
        }
    }
    if (qy == 1) {
        if (mask & 2) {
            if (r > (*y & 0xffff)) *y = (*y & ~0xffff) + r;
            if (south) ++count;
        }
        if (r > (*x & 0xffff)) {
            if (mask & 1) {
                if (south) *x += 0x800;
                else if (r > (*y & 0xffff)) *y = (*y & ~0xffff) + r;
            }
        } else if ((*x & 0xffff) > far && (mask & 8)) {
            if (south) *x -= 0x800;
            else if (r > (*y & 0xffff)) *y = (*y & ~0xffff) + r;
        }
    } else if (qy == 2) {
        if (mask & 0x2000) {
            if ((*y & 0xffff) > far) *y = (*y & ~0xffff) + far;
            if (north) ++count;
        }
        if (r > (*x & 0xffff)) {
            if (mask & 0x1000) {
                if (north) *x += 0x800;
                else if ((*y & 0xffff) > far) *y = (*y & ~0xffff) + far;
            }
        } else if ((*x & 0xffff) > far && (mask & 0x8000)) {
            if (north) *x -= 0x800;
            else if ((*y & 0xffff) > far) *y = (*y & ~0xffff) + far;
        }
    }
    return count;
}

static int bound(sim_state *s, world *w, unsigned object, int32_t *x, int32_t *y,
                  int variant)
{
    const world_object *o = &w->objects[object];
    const world_aperture *a;
    int32_t proposed_x = *x, proposed_y = *y;
    unsigned tries = 0;
    while (sim_path_check(s, w, *x, *y)) {
        int qx = (*x >> 14) % 4, qy = (*y >> 14) % 4;
        sim_new_nx_ny(s, w, object, x, y, qx, qy);
        if (++tries >= SIM_PATH_RETRIES_MAX)
            return cm_fail("NewPCBound", object, "path correction did not converge");
        if (*x == o->x && *y == o->y) break;
    }
    if (variant == 2) tries += monster_radius(s, w, object, x, y);
    s->collision_flags = 0;
    if (variant == 1 ? clear_fxo(s, w, object, x, y) :
                   sim_coll_object(s, w, object, x, y)) return -1;
    if (variant == 2 && s->collision_flags) ++tries;
    a = old_aperture(s, w, o);
    if (a && (proposed_x != *x || proposed_y != *y)) {
        if (proposed_x != *x) {
            if (*x < a->min_x) *x = a->min_x;
            if (*x > a->max_x) *x = a->max_x;
        }
        if (proposed_y != *y) {
            if (*y < a->min_y) *y = a->min_y;
            if (*y > a->max_y) *y = a->max_y;
        }
        if (variant == 2) s->collision_flags |= 4;
        else s->collision_flags = 1;
        if (variant == 1 ? clear_fxo(s, w, object, x, y) :
                       sim_coll_object(s, w, object, x, y)) return -1;
        if (variant == 2) { ++tries; s->collision_flags &= ~4u; }
    }
    if (variant == 2 && s->ai)
        return ai_monster_blocked(s->ai, s, w, object, tries);
    return 0;
}

int sim_new_pc_bound(sim_state *s, world *w, unsigned object, int32_t *x, int32_t *y)
{
    return bound(s, w, object, x, y, 0);
}

int sim_monster_bound(sim_state *s, world *w, unsigned object, int32_t *x, int32_t *y)
{
    return bound(s, w, object, x, y, 2);
}

/* Xox::ClearObject 045768..045AB4. Search at most the original eight
 * radial offsets, preserving floor_z even when no free centre is found. */
int sim_clear_object(sim_state *s, world *w, unsigned object)
{
    static const int16_t radial_search_v[8] = {0,8192,-8192,16384,-16384,24576,-24576,-32768};
    world_object *o;
    int32_t original_z, expected_floor, x, y, low_z, high_z;
    unsigned attempt;
    if (object >= w->object_count) return cm_fail("ClearObject", object, "invalid object");
    o = &w->objects[object];
    original_z = o->floor_z;
    low_z = original_z + 0x1000;
    high_z = original_z + o->z + o->height - 0x1000;
    if (sim_coll_flat(w, object, o->x, o->y)) return -1;
    expected_floor = o->floor_z;
    x = o->x; y = o->y;
    for (attempt = 0; attempt <= 8; ++attempt) {
        int blocked = 0;
        int tile;
        unsigned node, count = 0;
        if (sim_coll_flat(w, object, x, y)) return -1;
        if (o->floor_z != expected_floor) blocked = 1;
        else {
            if (bound(s, w, object, &x, &y, 1)) return -1;
            tile = (x >> 16) + (y >> 16) * CM_MAP_SIDE;
            if (tile < 0 || tile >= CM_MAP_CELLS)
                return cm_fail("ClearObject", object, "invalid tile");
            node = (unsigned)tile;
            while (node) {
                unsigned id;
                const world_object *b;
                if (node >= CM_CHAIN_NODES || ++count > CM_CHAIN_NODES)
                    return cm_fail("OMap", node, "invalid object chain");
                id = w->omap.nodes[node].object;
                if (id >= w->object_count) return cm_fail("OMap", node, "invalid object");
                b = &w->objects[id];
                if (id && id != object && (b->flags & 0x20000000u) &&
                    !(b->flags & OBJECT_PASSABLE) &&
                    x + 0x1f00 > b->x - b->unk_74AF0 && x - 0x1f00 < b->x + b->unk_74AF0 &&
                    y + 0x1f00 > b->y - b->unk_74AF0 && y - 0x1f00 < b->y + b->unk_74AF0 &&
                    b->z + b->floor_z + b->height > low_z &&
                    b->z + b->floor_z - b->height < high_z) {
                    if (b->targetable == 1 && b->person >= 0 &&
                        (unsigned)b->person < w->person_count && w->persons[b->person].status == 2) {
                        /* Dead actors do not block clearance. */
                    } else if (object == w->player_object && b->targetable == 2) {
                        if (b->person < 0 || (unsigned)b->person >= w->prop_count ||
                            w->prop_objects[b->person] != id)
                            return cm_fail("ClearObject", id, "invalid prop placement");
                        if (!w->prop_items[b->person]) blocked = 1;
                    } else blocked = 1;
                }
                node = w->omap.nodes[node].next;
            }
        }
        o->floor_z = original_z;
        if (!blocked) { o->x = x; o->y = y; break; }
        if (attempt < 8) {
            unsigned angle = (uint16_t)(o->heading + radial_search_v[attempt]) >> 5;
            x = o->x + sim_mul16(0x6000, w->trig.cos[angle]);
            y = o->y + sim_mul16(0x6000, w->trig.sin[angle]);
        }
    }
    return 0;
}
