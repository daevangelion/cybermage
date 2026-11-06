#include "ai.h"
#include "audio.h"
#include "events.h"
#include "../src/sim/sim_internal.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static ai_state a;
static sim_state s;
static events_state e;
static world w;
static ui_save_state save;

static void pools(void)
{
    static const uint32_t active[AI_SOUL_MAX] = {
        1, 0, UINT32_MAX, 2, 1, 1, UINT32_C(0x80000000), INT32_MAX, 0, 1
    };
    static const uint32_t value[AI_SOUL_MAX] = {
        46, 50000, 65535, 50000, UINT32_MAX, 32768, 0, 65536, 0xabcde, 0x12348001
    };
    unsigned i;
    memset(&a, 0, sizeof a); memset(&s, 0, sizeof s);
    memset(&e, 0, sizeof e); memset(&w, 0, sizeof w);
    memset(&save, 0, sizeof save);
    w.object_count = 64; w.prop_count = w.fxo_count = 30; w.top_level = 3;
    w.fxo_objects[13] = 42;
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        a.missiles[i].object = (int32_t)(i+1);
        save.unk_135ECC[i*12+1] = i+1;
    }
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        unsigned prop = i+20, object = i+13;
        uint32_t *p = save.unk_13610C+i*7;
        a.souls[i].object = (int32_t)object; a.souls[i].prop = (int32_t)prop;
        w.prop_objects[prop] = (uint16_t)object;
        w.prop_items[prop] = (int16_t)(200+i);
        p[0] = active[i]; p[1] = object; p[2] = prop; p[4] = value[i];
    }
}

static void print_records(void)
{
    unsigned i;
    for (i = 0; i < AI_SOUL_MAX*7; ++i)
        printf("%s%" PRIu32, i ? " " : "", save.unk_13610C[i]);
    putchar('\n');
    for (i = 0; i < AI_SOUL_MAX; ++i)
        printf("%s%d", i ? " " : "", w.prop_items[a.souls[i].prop]);
    putchar('\n');
}

static void collision_and_validation(void)
{
    unsigned i;
    int32_t x, y;
    pools();
    for (i = 0; i < AI_SOUL_MAX; ++i) save.unk_13610C[i*7] = 0;
    save.unk_13610C[0] = 1; save.unk_13610C[4] = 46;
    w.player_object = w.player_person = 1; w.person_count = 2;
    w.persons[0].object = 40; w.persons[1].object = 1;
    w.objects[1].person = 1;
    w.objects[1].x = w.objects[1].y = 0x180000;
    w.objects[40].active = w.objects[40].targetable = 1;
    w.objects[40].x = w.objects[40].y = 0x88000;
    w.objects[40].height = 0x8000; w.objects[40].z = 0x1000;
    w.objects[40].radius = w.objects[40].unk_74AF0 = 0x1000;
    w.objects[13].active = 1; w.objects[13].targetable = 2; w.objects[13].person = 20;
    w.objects[13].x = w.objects[13].y = 0x88100;
    w.objects[13].flags = UINT32_C(0x20080000);
    w.objects[13].unk_74AF0 = 0x1e00;
    w.objects[13].height = 0x2000; w.objects[13].z = 0x1000;
    e.definition_count = 46; e.definitions[46].kind = 16;
    s.ai = &a; s.events = &e;
    a.monster_count = 1;
    world_trig_init(&w.trig);
    assert(world_make_omap(&w) == 0);
    w.prop_items[20] = 0;
    x = y = 0x88010;
    assert(sim_coll_object(&s, &w, 40, &x, &y) == 0);
    assert(x != 0x88010 || y != 0x88010); /* The missing item mirror blocks. */
    assert(ai_save_import(&a, &w, &save) == 0);
    assert(w.prop_items[20] == 46 && w.fxo_objects[13] == 42);
    x = y = 0x88010;
    assert(sim_coll_object(&s, &w, 40, &x, &y) == 0);
    assert(x == 0x88010 && y == 0x88010); /* Item kind16 does not block NPCs. */
    w.prop_items[20] = 321;
    save.unk_13610C[9*7+5] = UINT32_C(0x80000000);
    assert(ai_save_import(&a, &w, &save) == -1);
    assert(w.prop_items[20] == 321 && w.fxo_objects[13] == 42);
    save.unk_13610C[9*7+5] = 0; save.unk_13610C[9*7+6] = 8;
    assert(ai_save_import(&a, &w, &save) == -1);
    assert(w.prop_items[20] == 321);
    save.unk_13610C[9*7+6] = 0; save.unk_13610C[9*7+2] = 13;
    assert(ai_save_import(&a, &w, &save) == -1);
    assert(w.prop_items[20] == 321);
    puts("Soul load: NPC item-kind16 pass-through, FXO identity and rejection atomicity passed");
}

static void loaded_records(const char *path, const char *data)
{
    cm_file file = {0};
    unsigned scene;
    assert(platform_file_read(&file, path) == 0);
    assert(ui_save_scene(file.data, file.size, &scene) == 0);
    world_rng_init(&w.rng, 44);
    assert(world_load_scene(&w, data, scene) == 0);
    assert(ai_init(&a, &w, 1) == 0 && ai_load_tables(&a, data) == 0);
    assert(world_finalize_scene(&w) == 0);
    assert(events_init(&e, &w) == 0);
    assert(e.definition_count >= 46 && e.definitions[46].kind == 16);
    ui_save_init(&save, &w);
    assert(ui_save_overlay(&save, &w, file.data, file.size) == 0);
    assert(ai_save_import(&a, &w, &save) == 0);
    print_records();
    world_close(&w); platform_file_release(&file);
}

int main(int argc, char **argv)
{
    assert(platform_init(1) == 0); snd_enable(0, 0);
    if (argc == 2 && !strcmp(argv[1], "--matrix")) {
        pools();
        assert(ai_save_import(&a, &w, &save) == 0);
        assert(w.fxo_objects[13] == 42);
        print_records();
    } else if (argc == 2 && !strcmp(argv[1], "--collision")) collision_and_validation();
    else if (argc == 5 && !strcmp(argv[1], "--save") && !strcmp(argv[3], "--data"))
        loaded_records(argv[2], argv[4]);
    else { platform_shutdown(); return 2; }
    platform_shutdown();
    return 0;
}
