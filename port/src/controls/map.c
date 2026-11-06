#include "controls.h"
#include "gfx.h"
#include "world.h"

#define MAP_X 432
#define MAP_Y 12
#define MAP_SCALE 6
#define MAP_PIXELS (CM_MAP_SIDE * MAP_SCALE)

static void point(uint8_t *pixels, int x, int y, uint8_t color)
{
    if (x >= MAP_X && x < MAP_X + MAP_PIXELS &&
        y >= MAP_Y && y < MAP_Y + MAP_PIXELS)
        gfx_store(pixels + y * CM_WIDTH + x, color);
}

/* Enhanced map: live level topology, not a replica of DOS AutoMap artwork. */
int controls_draw_map(const cm_controls *c, const world *w,
                       uint8_t pixels[CM_WIDTH * CM_HEIGHT])
{
    const world_object *o;
    unsigned x, y, side, i, level;
    int px, py;
    if (!c->map_visible) return 0;
    if (w->player_object >= w->object_count)
        return cm_fail("controls/map", w->player_object, "invalid player object");
    o = &w->objects[w->player_object];
    level = o->floor_z < 0 ? 0 : (unsigned)o->floor_z / 65536;
    if (level >= CM_MAP_LEVELS) level = CM_MAP_LEVELS - 1;
    for (y = 0; y < MAP_PIXELS; ++y) for (x = 0; x < MAP_PIXELS; ++x)
        point(pixels, MAP_X + (int)x, MAP_Y + (int)y, 0);
    for (y = 0; y < CM_MAP_SIDE; ++y) for (x = 0; x < CM_MAP_SIDE; ++x) {
        const world_cell *cell = &w->cells[level][y * CM_MAP_SIDE + x];
        int bx = MAP_X + (int)x * MAP_SCALE;
        int by = MAP_Y + (int)y * MAP_SCALE;
        if (w->cells[0][y * CM_MAP_SIDE + x].wall_sides == UINT32_MAX) continue;
        for (side = 0; side < 4; ++side) {
            if (!(cell->wall_sides & (255u << (side * 8)))) continue;
            for (i = 0; i < MAP_SCALE; ++i)
                point(pixels, bx + (side == 0 ? 0 : side == 2 ? MAP_SCALE - 1 : (int)i),
                       by + (side == 1 ? 0 : side == 3 ? MAP_SCALE - 1 : (int)i), 125);
        }
    }
    px = MAP_X + (int)((int64_t)o->x * MAP_SCALE / 65536);
    py = MAP_Y + (int)((int64_t)o->y * MAP_SCALE / 65536);
    for (i = 0; i < 5; ++i) {
        point(pixels, px + (int)i - 2, py, 255);
        point(pixels, px, py + (int)i - 2, 255);
        point(pixels, px + (int)((int64_t)w->trig.cos[o->heading >> 5] * i / 65536),
               py + (int)((int64_t)w->trig.sin[o->heading >> 5] * i / 65536), 255);
    }
    return 0;
}
