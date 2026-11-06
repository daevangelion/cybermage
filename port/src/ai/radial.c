#include "combat_internal.h"
#include "events.h"

/* CM_HI.EXE RadialDamage02B484 and PCZoneRadius02D478. The blast checks
 * centre, horizontal neighbour, vertical neighbour, diagonal in that order.
 * The horizontal visit leaves the wall-mask cursor changed for the vertical
 * test; preserving that original quirk also preserves damage/RNG order. */
static int cells_for(const world *w, unsigned source, unsigned cells[4], unsigned *level)
{
    const world_object *o;
    int x, y, z, dx = 0, dy = 0, open_left = 0, open_right = 0, open_up = 0, open_down = 0;
    unsigned cursor, count = 1, region;
    const world_aperture *door;
    if (source >= w->object_count) return cm_fail("RadialDamage", source, "invalid source object");
    o = &w->objects[source]; x = o->x >> 16; y = o->y >> 16;
    z = ai_add32(o->floor_z, o->z) >> 16;
    if (x < 0 || x >= 32 || y < 0 || y >= 32 || z < 0 || z >= CM_MAP_LEVELS)
        return cm_fail("RadialDamage", source, "blast outside world");
    *level = (unsigned)z; cursor = cells[0] = (unsigned)(y * 32 + x);
    region = w->cells[z][cursor].region;
    if (region >= 96) return cm_fail("RadialDamage", region, "invalid aperture region");
    door = &w->apertures[region];
    if (door->enabled) {
        if ((door->min_x >> 16) < x) open_left = 1;
        else if ((door->max_x >> 16) > x) open_right = 1;
        else if ((door->min_y >> 16) < y) open_up = 1;
        else if ((door->max_y >> 16) > y) open_down = 1;
    }
    if (x > 1 && ((o->x - 0x8000) >> 16) < x) dx = -1;
    else if (x < 30 && ((o->x + 0x8000) >> 16) > x) dx = 1;
    if (dx && (!(w->inertia_mask[z][cursor] & (dx < 0 ? 0x10 : 0x80)) ||
               (dx < 0 ? open_left : open_right))) {
        cursor = (unsigned)(y * 32 + x + dx); cells[count++] = cursor;
    }
    if (y > 1 && ((o->y - 0x8000) >> 16) < y) dy = -1;
    else if (y < 30 && ((o->y + 0x8000) >> 16) > y) dy = 1;
    if (dy && (!(w->inertia_mask[z][cursor] & (dy < 0 ? 2 : 0x2000)) ||
               (dy < 0 ? open_up : open_down))) {
        cursor = (unsigned)((y + dy) * 32 + x); cells[count++] = cursor;
        if (dx && !(w->inertia_mask[z][cursor] & (dx < 0 ? 0x10 : 0x80)))
            cells[count++] = (unsigned)((y + dy) * 32 + x + dx);
    }
    return (int)count;
}

int ai_pc_zone_radius(const world *w, unsigned source, unsigned tile)
{
    unsigned cells[4], level, i;
    int count;
    if (tile >= CM_MAP_CELLS) return cm_fail("PCZoneRadius", tile, "invalid target tile");
    count = cells_for(w, source, cells, &level);
    if (count < 0) return -1;
    for (i = 0; i < (unsigned)count; ++i) if (cells[i] == tile) return 1;
    return 0;
}

static unsigned tier_for(const ai_state *a, const world_object *source,
                          const world_object *target, unsigned zone)
{
    int64_t x = (int64_t)target->x - source->x, y = (int64_t)target->y - source->y;
    int64_t z = (int64_t)target->floor_z + target->z - source->floor_z - source->z;
    unsigned tier = 0, i;
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    if (z < 0) z = -z;
    for (i = 0; i < 3; ++i) {
        int32_t radius = a->zone_radius[zone][i];
        if (x < radius && y < radius && z < (int64_t)radius + target->z) ++tier;
    }
    return tier;
}

int ai_freeze_monster(ai_state *a, sim_state *s, world *w, unsigned object,
                       unsigned tier, unsigned row)
{
    world_person *p;
    int32_t duration;
    unsigned resistance = 0;
    if (!a->tables_loaded || object >= w->object_count || tier >= 3 || row >= 24 ||
        w->objects[object].person < 0 || (unsigned)w->objects[object].person >= w->person_count)
        return cm_fail("FreezeMonster", object, "invalid freeze target/dice");
    p = &w->persons[w->objects[object].person];
    duration = ai_do_dice(&w->rng, a->zone_dice[row][tier]);
    if (!duration) return 0;
    if ((unsigned)w->objects[object].person != w->player_person) {
        if (p->type < 0 || (unsigned)p->type >= a->monster_count)
            return cm_fail("FreezeMonster", object, "invalid monster definition");
        resistance = a->monsters[p->type].resistance[4];
    }
    if (resistance) duration = ai_scale32(duration, 100 - (int32_t)resistance, 100);
    if (duration) {
        p->unk_0058 = duration;
        if ((unsigned)w->objects[object].person == w->player_person) a->pc_flash = 3;
    }
    (void)s;
    return 0;
}

int ai_radial_damage(ai_state *a, sim_state *s, world *w, unsigned source,
                      unsigned zone, int32_t shooter, unsigned row,
                      unsigned resistance_index, int32_t force)
{
    unsigned cells[4], level, i;
    int count;
    if (!a->tables_loaded || zone >= 34 || row >= 24 || resistance_index >= 8)
        return cm_fail("RadialDamage", zone, "invalid blast table selection");
    count = cells_for(w, source, cells, &level);
    if (count < 0) return -1;
    for (i = 0; i < (unsigned)count; ++i) {
        unsigned node = cells[i], budget = CM_CHAIN_NODES;
        const world_cell *cell = &w->cells[level][node];
        int32_t ceiling = cell->ceiling ? (int32_t)(level * 65536 + 65535) : 0x40000;
        int32_t floor = cell->floor ? (int32_t)(level * 65536) : 0;
        while (node) {
            unsigned id, tier, person, resistance;
            int32_t damage;
            world_object *o;
            world_person *p;
            if (!budget-- || node >= CM_CHAIN_NODES)
                return cm_fail("RadialDamage", node, "invalid/cyclic object chain");
            id = w->omap.nodes[node].object;
            if (id >= w->object_count) return cm_fail("RadialDamage", id, "invalid chained object");
            o = &w->objects[id];
            if (!id || o->targetable != 1 || (int32_t)id == shooter) {
                node = w->omap.nodes[node].next; continue;
            }
            if (o->person < 0 || (unsigned)o->person >= w->person_count)
                return cm_fail("RadialDamage", id, "blast target has no PERSON");
            person = (unsigned)o->person; p = &w->persons[person];
            if (person != w->player_person && p->status != 2 &&
                ceiling > o->floor_z && (int64_t)o->floor_z + 2 * (int64_t)o->height > floor) {
                tier = tier_for(a, &w->objects[source], o, zone);
                if (tier) {
                    if (p->type < 0 || (unsigned)p->type >= a->monster_count)
                        return cm_fail("RadialDamage", person, "invalid monster definition");
                    damage = ai_do_dice(&w->rng, a->zone_dice[row][tier - 1]);
                    resistance = a->monsters[p->type].resistance[resistance_index];
                    if (resistance) damage = ai_scale32(damage, 100 - (int32_t)resistance, 100);
                    if (damage) {
                        if (ai_damage_monster(a, w, person, damage, shooter)) return -1;
                        if (force && ai_blast_physics(a, w, id, source, force)) return -1;
                        if (row == 16 && ai_freeze_monster(a, s, w, id, tier - 1, 12)) return -1;
                    }
                }
            }
            node = w->omap.nodes[node].next;
        }
    }
    return 0;
}

int ai_pc_radial_damage(ai_state *a, sim_state *s, world *w, unsigned source,
                         unsigned zone, unsigned row, int double_hit,
                         int32_t armor, int32_t erosion)
{
    unsigned cells[4], level, i, tier;
    int count, found = 0, twice;
    int32_t damage, floor, ceiling;
    world_object *pc;
    unsigned tile;
    if (!a->tables_loaded || zone >= 34 || row >= 24 || w->player_object >= w->object_count)
        return cm_fail("PCRadialDamage", zone, "invalid player or blast table");
    pc = &w->objects[w->player_object];
    if (pc->x < 0 || pc->x >= 0x200000 || pc->y < 0 || pc->y >= 0x200000)
        return cm_fail("PCRadialDamage", w->player_object, "player outside map");
    tile = (unsigned)((pc->y >> 16) * 32 + (pc->x >> 16));
    count = cells_for(w, source, cells, &level);
    if (count < 0) return -1;
    for (i = 0; i < (unsigned)count; ++i) if (cells[i] == tile) found = 1;
    if (!found) return 0;
    ceiling = w->cells[level][tile].ceiling ? (int32_t)(level * 65536 + 65535) : 0x40000;
    floor = w->cells[level][tile].floor ? (int32_t)(level * 65536) : 0;
    if (ceiling < pc->floor_z || (int64_t)pc->floor_z + s->body_height < floor) return 0;
    tier = tier_for(a, &w->objects[source], pc, zone);
    twice = s->pc_mode == 7 && double_hit && tier == 3;
    if (tier && s->unk_144b34) --tier;
    if (!tier) return 0;
    if (a->pc_learn_spell >= 0) {
        if (!a->events || a->pc_learn_spell >= 64)
            return cm_fail("PCRadialDamage", (uint32_t)a->pc_learn_spell, "invalid spell acquisition");
        a->events->spells[(unsigned)a->pc_learn_spell / 8] |= (uint8_t)(1u << ((unsigned)a->pc_learn_spell % 8));
    }
    damage = ai_do_dice(&w->rng, a->zone_dice[row][tier - 1]);
    if (twice) damage = ai_add32(damage, ai_do_dice(&w->rng, a->zone_dice[row][tier - 1]));
    if (damage && ai_damage_person(a, w, w->player_person, damage, armor, erosion)) return -1;
    return damage;
}
