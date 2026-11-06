#include "world.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static world w;

static void chains(void)
{
    unsigned i, cell = 6 * 32 + 5;
    world_make_omap(&w);
    for (i = 1; i <= 3; ++i) {
        w.objects[i].x = 0x58000;
        w.objects[i].y = 0x68000;
        w.objects[i].radius = 0x8000;
    }
    world_omap_insert(&w, 1);
    world_omap_insert(&w, 1);
    assert(w.omap.nodes[cell].object == 1 && !w.omap.nodes[cell].next);
    assert(w.omap.nodes[0].next == 1024);
    world_omap_insert(&w, 2);
    assert(w.omap.nodes[cell].next == 1024);
    assert(w.omap.nodes[1024].object == 2);
    world_omap_delete(&w, 1);
    assert(!w.omap.nodes[cell].object && w.omap.nodes[cell].next == 1024);
    world_omap_insert(&w, 3);
    assert(w.omap.nodes[cell].object == 3 && w.omap.nodes[0].next == 1025);
    world_omap_delete(&w, 2);
    assert(!w.omap.nodes[cell].next && w.omap.nodes[0].next == 1024);
    w.omap.nodes[0].next = 0;
    world_omap_insert(&w, 2);
    assert(!w.omap.nodes[cell].next && w.omap.nodes[cell].object == 3);
}

int main(int argc, char **argv)
{
    int32_t a, b, c, d;
    if (argc == 6 && !strcmp(argv[1], "math")) {
        a = (int32_t)strtol(argv[2], NULL, 0);
        b = (int32_t)strtol(argv[3], NULL, 0);
        c = (int32_t)strtol(argv[4], NULL, 0);
        d = (int32_t)strtol(argv[5], NULL, 0);
        printf("%u %" PRId32 "\n", (unsigned)cm_atan64k(a,b,c,d), cm_distance(a,b,c,d));
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "scenes")) {
        unsigned scene;
        for (scene = 1; scene <= 11; ++scene) {
            if (world_load_scene(&w, argv[2], scene)) return 1;
            printf("scene %u objects=%u persons=%u panels=%u\n", scene,
                   w.object_count, w.person_count, w.panel_count);
            world_close(&w);
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "trig")) {
        unsigned i;
        world_trig_init(&w.trig);
        for (i = 0; i < CM_TRIG_SIZE; ++i)
            printf("%" PRId32 " %" PRId32 " %" PRId32 "\n", w.trig.tan[i], w.trig.sin[i], w.trig.cos[i]);
        return 0;
    }
    if (argc == 8 && !strcmp(argv[1], "sight")) {
        uint16_t angle = (uint16_t)strtoul(argv[2], NULL, 0);
        int32_t z = (int32_t)strtol(argv[5], NULL, 0);
        int32_t tz = (int32_t)strtol(argv[6], NULL, 0);
        uint32_t flags = (uint32_t)strtoul(argv[7], NULL, 0);
        int32_t hx, hy, hz, hd, distance;
        unsigned level;
        int sight, missile;
        w.object_count = 3; w.top_level = 3;
        w.objects[1].x = w.objects[1].y = 0x58000;
        w.objects[2].x = (int32_t)strtol(argv[3], NULL, 0);
        w.objects[2].y = (int32_t)strtol(argv[4], NULL, 0);
        world_trig_init(&w.trig);
        if (flags != UINT32_MAX) {
            for (level = 0; level < 4; ++level)
                w.cells[level][5 * 32 + 5].wall_sides = 0x01010101;
            w.map_id_flags[1] = flags;
        }
        distance = cm_distance(w.objects[1].x,w.objects[1].y,
                               w.objects[2].x,w.objects[2].y);
        sight = world_ray_sight(&w,1,2,distance,z,tz,angle);
        missile = world_ray_missile(&w,1,2,distance,z,tz,angle,&hx,&hy,&hz,&hd);
        printf("%d %d %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 "\n",
               sight,missile,hx,hy,hz,hd);
        return 0;
    }
    assert(world_load_map(&w, NULL, 0) == -1);
    chains();
    puts("world map rejection and spatial-chain vectors passed");
    return 0;
}
