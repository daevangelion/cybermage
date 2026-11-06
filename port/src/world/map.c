#include "world.h"

/* CM_HI.EXE LoadMAP 0x27960..0x27BD7; docs/re/formats.md. */
int world_load_map(world *w, const uint8_t *p, uint32_t size)
{
    unsigned i, level, tile;
    if (!w || !p || size != 54790)
        return cm_fail("SCENE.RSC/map", size, "invalid map section length");
    if (cm_u16(p) != CM_MAP_SIDE || cm_u16(p + 2) != CM_MAP_SIDE ||
        cm_u16(p + 4) != CM_MAP_LEVELS)
        return cm_fail("SCENE.RSC/map", 0, "unsupported map dimensions");
    for (i = 0; i < 256; ++i) {
        w->map_id_texture[i] = cm_u16(p + 6 + i * 2);
        w->map_id_flags[i] = cm_u32(p + 518 + i * 4);
    }
    for (level = 0; level < CM_MAP_LEVELS; ++level)
        for (tile = 0; tile < CM_MAP_CELLS; ++tile) {
            world_cell *c = &w->cells[level][tile];
            unsigned cell = level * CM_MAP_CELLS + tile;
            const uint8_t *a = p + 17926 + cell * 5;
            c->wall_sides = cm_u32(p + 1542 + cell * 4);
            c->floor = a[0];
            c->ceiling = a[1];
            c->unk_C5090 = a[2];
            c->region = a[3];
            c->light = a[4];
            w->inertia_mask[level][tile] = cm_u16(p + 38406 + cell * 2);
            w->path_mask[level][tile] = cm_u16(p + 46598 + cell * 2);
        }
    w->top_level = CM_MAP_LEVELS - 1;
    w->unk_130D20 = w->top_level * 65536 + 65535;
    return 0;
}
