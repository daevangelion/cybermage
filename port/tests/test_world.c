#include "world.h"
#include "../src/world/world_internal.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static world w;

static void bad_placement(unsigned section,unsigned type_offset,unsigned field)
{
    uint8_t record[60]={1},empty[30]={0};
    world_span old=w.scene_sections[section],old_fxo=w.scene_sections[2];
    unsigned i,count=cm_u32(old.data);
    for(i=1;i<=count;++i)
        if((int16_t)cm_u16(old.data+i*30+type_offset)>0) break;
    assert(i<=count);
    memcpy(record+30,old.data+i*30,30);
    record[30+field]=0;record[31+field]=255;record[32+field]=255;record[33+field]=127;
    w.scene_sections[section].data=record;w.scene_sections[section].size=sizeof record;
    if(section==1) {
        w.scene_sections[2].data=empty;w.scene_sections[2].size=sizeof empty;
    }
    assert(world_load_placements(&w) == -1);
    w.scene_sections[section]=old;w.scene_sections[2]=old_fxo;
}

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
            if (scene == 1) {
                const world_texture *icon;
                unsigned object = w.player_object, type = (unsigned)w.persons[0].type;
                unsigned prop,base;
                assert(world_set_person(&w,0,type,99,0x50000,0x60000,0,0x20000,0x1234) == 0);
                bad_placement(2,24,0);
                bad_placement(2,24,8);
                bad_placement(2,24,16);
                bad_placement(1,20,0);
                bad_placement(1,20,16);
                {
                    uint8_t monsters[109*276];
                    world_span old=w.monster_definitions;
                    assert(old.size<=sizeof monsters);
                    memcpy(monsters,old.data,old.size);
                    monsters[type*276+0x17]=monsters[type*276+0x18]=255;
                    w.monster_definitions.data=monsters;
                    assert(world_set_person(&w,0,type,99,0x50000,0x60000,0,0x20000,0) == -1);
                    w.monster_definitions=old;
                }
                {
                    uint8_t bad[351]={1};
                    unsigned n;
                    assert(world_section_records((world_span){bad,sizeof bad},351,
                                                 "SCENE.RSC/PERSON",123,&n) == -1);
                }
                {
                    uint8_t bad[702]={1};
                    world_span old=w.scene_sections[3];
                    memcpy(bad+351,old.data+351,351);
                    bad[351+319]=0;bad[351+320]=255;bad[351+321]=255;bad[351+322]=127;
                    w.scene_sections[3]=(world_span){bad,sizeof bad};
                    assert(world_load_persons(&w) == -1);
                    w.scene_sections[3]=old;
                }
                assert(w.persons[0].state == 2 && w.persons[0].unk_001E == 10);
                assert(w.persons[0].unk_00BC == 1 && w.objects[object].heading == 0x1234);
                assert(w.objects[object].person == 0 && w.objects[object].active == 1);
                assert(world_set_person(&w,0,type,99,0x7FFFFF00,0x60000,0,0x20000,0) == -1);
                assert(world_set_person(&w,0,type,99,0x50000,0x60000,0,0x20000,0x1234) == 0);
                for(prop=1;prop<w.prop_definitions.size/1051;++prop)
                    if(cm_u16(w.prop_definitions.data+prop*1051+28)==12) break;
                assert(prop<w.prop_definitions.size/1051);
                assert(world_prop_texture(&w,w.prop_definitions.data+prop*1051,&base) == 0);
                assert(base+31<w.texture_count);
                icon=&w.textures[base+31];
                assert(icon->resource == 12 && icon->frame == 15 && icon->rotations == 32);
                {
                    unsigned texture = w.fxo_textures[0], revision = w.textures[texture].revision;
                    assert(world_load_fxo_news(&w,12,0,w.fxo_objects[0]) == 0);
                    assert(w.objects[w.fxo_objects[0]].texture == texture);
                    assert(w.textures[texture].revision == revision + 1);
                    assert(world_load_fxo_news(&w,3,0,w.fxo_objects[0]) == -1);
                }
            }
            world_close(&w);
        }
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "rng")) {
        unsigned i;
        world_rng r = {0};
        r.word[0] = (uint16_t)strtoul(argv[2], NULL, 0);
        r.word[1] = (uint16_t)strtoul(argv[3], NULL, 0);
        r.word[2] = (uint16_t)strtoul(argv[4], NULL, 0);
        for (i = 0; i < 20; ++i) {
            int32_t value = world_random(&r);
            printf("%" PRId32 " %u %u %u\n", value,
                   (unsigned)r.word[0], (unsigned)r.word[1], (unsigned)r.word[2]);
        }
        return 0;
    }
    if (argc == 7 && !strcmp(argv[1], "rotate")) {
        int32_t x = (int32_t)strtol(argv[2], NULL, 0);
        int32_t y = (int32_t)strtol(argv[3], NULL, 0);
        int32_t cx = (int32_t)strtol(argv[4], NULL, 0);
        int32_t cy = (int32_t)strtol(argv[5], NULL, 0);
        uint16_t angle = (uint16_t)strtoul(argv[6], NULL, 0);
        world_trig_init(&w.trig);
        cm_fixed_rotate(&w.trig,&x,&y,cx,cy,angle);
        printf("%" PRId32 " %" PRId32 "\n",x,y);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "state")) {
        unsigned level, tile, i;
        if (world_load_scene(&w, argv[2], (unsigned)strtoul(argv[3], NULL, 0))) return 1;
        printf("counts %u %u\n",w.object_count,w.person_count);
        printf("texture_count %u\n",w.texture_count);
        for (i = 0; i < 256; ++i)
            printf("map_id %u %" PRIu32 " %u\n",i,w.map_id_flags[i],(unsigned)w.map_id_texture[i]);
        for (i = 0; i < 32; ++i)
            printf("light %u %d %u\n",i,(int)w.light_param[i],(unsigned)w.light_type[i]);
        for (i = 0; i < 16; ++i)
            printf("curve %u %" PRId32 " %" PRId32 " %" PRId32 "\n",
                   i,w.curve_base[i],w.curve_shift[i],w.curve_scale[i]);
        for (i = 0; i < w.texture_count; ++i) {
            const world_texture *t = &w.textures[i];
            printf("texture %u %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32
                   " %" PRId32 " %" PRId32 "\n",i,t->width,t->height,
                   t->half_w,t->base,t->depth,t->frame_height);
        }
        for (i = 0; i < CM_CHAIN_NODES; ++i) {
            printf("omap %u %u %u\n",i,(unsigned)w.omap.nodes[i].next,(unsigned)w.omap.nodes[i].object);
            printf("flatmap %u %u %u\n",i,(unsigned)w.flatmap.nodes[i].next,(unsigned)w.flatmap.nodes[i].object);
        }
        for (level = 0; level < 4; ++level)
            for (tile = 0; tile < 1024; ++tile) {
                const world_cell *c = &w.cells[level][tile];
                printf("map %u %" PRIu32 " %u %u %u %u %u %u %u\n",
                       level*1024+tile,c->wall_sides,(unsigned)c->floor,
                       (unsigned)c->ceiling,(unsigned)c->unk_C5090,
                       (unsigned)c->region,(unsigned)c->light,
                       (unsigned)w.inertia_mask[level][tile],(unsigned)w.path_mask[level][tile]);
            }
        for (i = 0; i < w.object_count; ++i) {
            const world_object *o = &w.objects[i];
            printf("object %u %" PRIu32 " %" PRId32 " %" PRId32 " %" PRId32
                   " %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32
                   " %u %u %d %u %u %d\n",i,o->flags,o->x,o->y,o->z,o->floor_z,
                   o->radius,o->height,o->unk_6F220,o->unk_71E88,
                   (unsigned)o->heading,(unsigned)o->texture,(int)o->person,
                   (unsigned)o->active,(unsigned)o->targetable,(int)o->rotations);
        }
        for (i = 0; i < w.person_count; ++i) {
            const world_person *p = &w.persons[i];
            printf("person %u %d %d %u %d %d %u %u %d\n",i,(int)p->object,
                   (int)p->type,p->monster_index,(int)p->state,(int)p->status,
                   (unsigned)p->unk_00BC,(unsigned)p->unk_00BD,(int)p->unk_00BE);
        }
        world_close(&w);
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
    w.object_count = 2;
    w.objects[1].active = 1;
    w.objects[1].y = -1;
    assert(world_make_omap(&w) == -1);
    w.objects[1].y = 0x58000;
    w.objects[1].x = 0x7FFFFF00;
    assert(world_make_omap(&w) == -1);
    w.cells[1][7].floor = 1; /* original downward test skips intervening floors */
    assert(world_tile_sight(&w,7,2,0,0,8,0) == 0);
    w.cells[2][7].floor = 1;
    assert(world_tile_sight(&w,7,2,0,0,8,0) == -1);
    {
        world_rng r = {.word = {1,2,3}};
        assert(world_rnd(&r,0) == 0 && r.word[0] == 1 && r.word[1] == 2 && r.word[2] == 3);
        world_rng_seed(&r,0x1234);
        assert(r.word[0] == 1 && r.word[1] == 0x1234 && r.word[2] == 3);
    }
    puts("world map rejection and spatial-chain vectors passed");
    return 0;
}
