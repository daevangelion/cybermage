#include "ai.h"
#include "audio.h"
#include "events.h"
#include "../src/sim/sim_internal.h"
#include <assert.h>
#include <string.h>

static world w;
static ai_state a;
static events_state e;
static sim_state s;
static cm_input in;
static const char *data_dir;

static void scene(unsigned number)
{
    unsigned tile = 5 + 5 * CM_MAP_SIDE;
    snd_kill(&w);
    memset(&w, 0, sizeof w);
    memset(&a, 0, sizeof a);
    memset(&e, 0, sizeof e);
    assert(!ai_load_tables(&a, data_dir));
    sim_init(&s, 100);
    w.scene = number; w.object_count = 2; w.person_count = 1;
    w.player_object = 1; w.top_level = 3; w.unk_130D20 = 0x3ffff;
    w.objects[1].x = w.objects[1].y = 0x58000;
    w.objects[1].z = 0x6000; w.objects[1].height = 0x2000;
    w.objects[1].active = w.objects[1].targetable = 1;
    w.objects[1].unk_74AF0 = 0x1e00;
    w.persons[0].object = 1; w.persons[0].unk_0038 = 40 * 65536 + 40;
    w.cells[0][tile].ceiling = 1;
    world_trig_init(&w.trig); world_rng_init(&w.rng, 44);
    s.ai = &a; s.events = &e; a.events = &e; a.difficulty = 1;
    assert(!sim_start_scene(&s, &w));
    assert(a.swim_check == s.swim_check && a.swim_damage == s.swim_damage);
    assert(a.swim_panel == s.swim_panel && a.swim_art == s.swim_art);
    w.cells[0][tile].floor = (uint8_t)s.swim_panel;
}

int main(int argc, char **argv)
{
    world_rng expected;
    uint32_t deadline;
    int heal;
    assert(argc == 2);
    data_dir = argv[1];
    assert(!platform_init(1) && !snd_open(data_dir));
    scene(1);
    expected = w.rng;
    deadline = 120u + (uint32_t)world_rnd(&expected, 40);
    assert(!sim_frame(&s, &w, &in, 100));
    assert(s.unk_144b50 == 1 && s.unk_144b34 == -0x4800);
    assert(s.swim_damage_time == deadline);
    assert(!memcmp(expected.word, w.rng.word, sizeof expected.word));
    assert(!sim_frame(&s, &w, &in, 101));
    assert(s.swim_damage_time == deadline);
    w.cells[0][5 + 5 * CM_MAP_SIDE].floor = 1;
    assert(!sim_frame(&s, &w, &in, 102));
    assert(!s.unk_144b50 && s.unk_144b34 > -0x4800);
    scene(7);
    w.persons[0].unk_0038 = 40 * 65536 + 20;
    assert(!sim_frame(&s, &w, &in, 100));
    deadline = s.swim_damage_time + 1;
    expected = w.rng;
    heal = world_rnd(&expected, 10);
    (void)world_rnd(&expected, 60);
    assert(!sim_frame(&s, &w, &in, deadline));
    assert((uint16_t)w.persons[0].unk_0038 == 20 + heal);
    assert(!memcmp(expected.word, w.rng.word, sizeof expected.word));
    scene(4);
    assert(!sim_frame(&s, &w, &in, 100));
    assert(!sim_frame(&s, &w, &in, s.swim_damage_time + 1));
    assert((uint16_t)w.persons[0].unk_0038 < 40);
    scene(1);
    assert(!snd_sample_load(3) && !snd_sample_load(4));
    a.pc_sound = -1;
    s.unk_144aec = 8;
    assert(!sim_swim_step(&s, &w));
    assert(s.swim_loop >= 0);
    s.unk_144aec = 0;
    assert(!sim_swim_step(&s, &w));
    assert(s.swim_loop == -1 && a.pc_sound >= 0);
    assert(!snd_done(a.pc_sound, 3));
    snd_close(); platform_shutdown();
    return 0;
}
