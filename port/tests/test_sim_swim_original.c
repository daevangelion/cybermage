#include "../src/sim/sim_internal.h"
#include "ai.h"
#include "audio.h"
#include <inttypes.h>
#include <stdio.h>

static world w;
static sim_state s;
static ai_state a;

int main(int argc, char **argv)
{
    unsigned frame;
    unsigned scene = argc == 2 && argv[1][0] == '7' && !argv[1][1] ? 7 : 1;
    if (argc > 2 || (argc == 2 && (!argv[1][0] || argv[1][1] ||
        (argv[1][0] != '1' && argv[1][0] != '7')))) return 2;
    w.scene = scene; w.object_count = 2; w.person_count = 1;
    w.unk_5E7A8 = scene == 7;
    w.player_object = 1; w.top_level = 3;
    w.objects[1].x = 22 * 65536 + 32768;
    w.objects[1].y = 18 * 65536 + 32768;
    w.objects[1].floor_z = 2 * 65536;
    w.objects[1].z = 24576;
    w.persons[0].unk_0038 = 40 * 65536 + (scene == 7 ? 30 : 40);
    w.cells[2][18 * CM_MAP_SIDE + 22].floor = scene == 7 ? 30 : 45;
    w.cells[2][18 * CM_MAP_SIDE + 26].floor = 81;
    world_rng_init(&w.rng, 44);
    sim_init(&s, 100); s.ai = &a; a.pc_sound = -1;
    if (sim_start_scene(&s, &w)) return 1;
    snd_enable(0, 0);
    for (frame = 0; frame < 160; ++frame) {
        w.objects[1].x = (frame % 40 < 30 ? 22 : 26) * 65536 + 32768;
        s.timer_ticks = 100 + frame * 2;
        s.unk_144b1c = frame == 55;
        s.flight_mode = frame == 90;
        s.pc_mode = frame == 125 ? 7 : 5;
        s.unk_144aec = frame % 3 ? 8 : 0;
        if (sim_swim_step(&s, &w)) return 1;
        printf("%u %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32
               " %" PRId32 " %" PRIu32 " %u %u %u %u\n", frame,
               s.unk_144b50, s.unk_144b34, s.unk_144b3c, s.unk_144afc,
               s.body_height, s.swim_damage_time, (unsigned)(uint16_t)w.persons[0].unk_0038,
               w.rng.word[0], w.rng.word[1], w.rng.word[2]);
    }
    return 0;
}
