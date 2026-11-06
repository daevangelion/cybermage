#include "sim.h"
#include "ai.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static world w;
static ai_state a;
static sim_state s;

int main(int argc, char **argv)
{
    unsigned angle, panel, i;
    int result;
    if (argc != 3 && argc != 4) return 2;
    angle = (unsigned)strtoul(argv[1], NULL, 0);
    panel = (unsigned)strtoul(argv[2], NULL, 0);
    world_trig_init(&w.trig);
    w.top_level = 3; w.unk_130D20 = 0x3ffff; w.object_count = 3;
    for (i = 0; i < CM_MAP_CELLS; ++i) w.cells[0][i].floor = w.cells[0][i].ceiling = 1;
    w.objects[1].x = w.objects[1].y = 0x58000;
    w.objects[1].floor_z = 0x8000;
    w.objects[1].z = 0x100;
    w.objects[1].heading = (uint16_t)angle;
    w.objects[2].x = 0x58000 + w.trig.cos[angle >> 5];
    w.objects[2].y = 0x58000 + w.trig.sin[angle >> 5];
    w.objects[2].radius = 0x8000;
    w.objects[2].z = 0x10000;
    w.objects[2].unk_71E88 = 0x10000;
    w.objects[2].heading = (uint16_t)(angle + (panel == 2 ? 8192 : 0));
    if (panel) w.objects[2].flags = 0x08000000;
    a.missiles[0].active = 1;
    a.missiles[0].object = 1;
    a.missiles[0].type = 2;
    a.missiles[0].speed = 0x20000;
    a.missile_objects[0] = 2;
    a.missile_count = 1;
    s.ai = &a; s.frame_ticks = argc == 4 ? (int32_t)strtoul(argv[3], NULL, 0) : 2;
    if (!s.frame_ticks) {
        w.objects[2].x = w.objects[1].x;
        w.objects[2].y = w.objects[1].y;
    }
    result = sim_coll_fire(&s, &w, 0);
    if (!s.frame_ticks) {
        assert(result == 2);
        assert(w.objects[1].floor_z == INT32_MIN + 0x7fff);
    }
    printf("%d %d %d %d %u %d %d %d\n", result,w.objects[1].x,w.objects[1].y,
        w.objects[1].floor_z,(unsigned)w.objects[1].heading,a.missiles[0].target,
        a.missiles[0].vertical,a.missiles[0].ticks);
    return result == SIM_COLL_ERROR ? 1 : 0;
}
