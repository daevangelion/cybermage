#include "ai.h"
#include "audio.h"
#include "events.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ai_state a;
static sim_state s;
static events_state e;
static world w;
static unsigned backsteps;
static unsigned moves;

/* The original-x86 control intercepts these same external boundaries. */
int __wrap_ai_sight_person(const ai_state *actors, sim_state *simulation,
                          world *game, unsigned source, unsigned target, int32_t range)
{
    (void)actors; (void)simulation; (void)game;
    (void)source; (void)target; (void)range;
    return 0;
}
int __wrap_ai_move_object(ai_state *actors, sim_state *simulation, world *game,
                         unsigned person, int tick, int moving)
{
    (void)actors; (void)simulation; (void)game;
    (void)person; (void)tick; (void)moving;
    ++moves;
    return 0;
}
void __wrap_ai_track_target(world *game, unsigned object, unsigned other, int offset)
{
    (void)game; (void)object; (void)other; (void)offset;
}
void __wrap_ai_update_pc_object(world *game, int tick) { (void)game; (void)tick; }
int __wrap_ai_launch_missile(ai_state *actors, sim_state *simulation, world *game,
                            unsigned source, unsigned target, unsigned type)
{
    (void)actors; (void)simulation; (void)game;
    (void)source; (void)target; (void)type;
    return 0;
}
int __wrap_sim_jumble_bound(sim_state *simulation, world *game, unsigned object,
                            int32_t dx, int32_t dy, int collide)
{
    (void)simulation; (void)game; (void)object; (void)dx; (void)dy; (void)collide;
    ++backsteps;
    return 0;
}

static unsigned argument(const char *text, unsigned maximum)
{
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    assert(*text && !*end && value <= maximum);
    return (unsigned)value;
}

static void checkpoint(void)
{
    const world_person *p = &w.persons[1];
    printf("%d %d %u %d %d %d %u %u %u %u %u\n", p->state, p->unk_0028,
           (unsigned)p->unk_00BC, p->unk_0034, p->unk_001E, p->unk_001A,
           w.rng.word[0], w.rng.word[1], w.rng.word[2], backsteps, moves);
}

int main(int argc, char **argv)
{
    unsigned actor_kind, target_kind, alternate, seed, i;
    if (argc != 5 && argc != 6) return 2;
    if (argc == 6 && strcmp(argv[5], "--hurt") && strcmp(argv[5], "--death") &&
        strcmp(argv[5], "--choose") && strcmp(argv[5], "--choose-melee")) return 2;
    actor_kind = argument(argv[1], 17); target_kind = argument(argv[2], 17);
    alternate = argument(argv[3], 1); seed = argument(argv[4], UINT16_MAX);
    assert(platform_init(1) == 0); snd_enable(0, 0);
    w.scene = 1; w.object_count = 3; w.person_count = 2; w.player_object = 1;
    w.rng.word[0] = 3000; w.rng.word[1] = (uint16_t)seed; w.rng.word[2] = 29999;
    a.tables_loaded = 1; a.monster_count = 2; a.events = &e; a.difficulty = 1;
    a.monsters[0].kind = (uint8_t)actor_kind;
    a.monsters[0].attack[1] = a.monsters[0].attack[2] = 1;
    a.monsters[0].attack[5] = (uint8_t)alternate;
    a.monsters[0].accuracy = 100; a.monsters[0].range = 0x40000;
    a.monsters[0].fire_range = 0x40000;
    a.monsters[0].unk_007C = 24; a.monsters[0].unk_007E = 1;
    a.monsters[1].kind = (uint8_t)target_kind;
    s.ai = &a; s.events = &e; s.bp_limit_flag = 1;
    s.pc_mode = target_kind == 17 ? 7 : 5;
    for (i = 0; i < 2; ++i) {
        w.persons[i].object = (int16_t)(i+1); w.persons[i].unk_0038 = 40;
        w.persons[i].unk_0024 = 100;
        w.persons[i].unk_004C = w.persons[i].unk_0050 = -1;
        w.objects[i+1].active = 1; w.objects[i+1].person = (int16_t)i;
        w.objects[i+1].x = w.objects[i+1].y = 0x88000+(int32_t)i*0x1000;
        w.objects[i+1].z = 0x1000; w.objects[i+1].height = 0x8000;
    }
    w.persons[0].type = 1;
    w.persons[1].state = 5; w.persons[1].target = 0;
    w.persons[1].distance = 0x20000;
    if (argc == 6) {
        if (!strcmp(argv[5], "--choose") || !strcmp(argv[5], "--choose-melee")) {
            w.persons[1].state = 1;
            if (!strcmp(argv[5], "--choose-melee")) a.monsters[0].melee_range = 0x30000;
        } else {
            w.persons[1].state = !strcmp(argv[5], "--hurt") ? 6 : 7;
            w.persons[1].unk_001E = 2;
        }
    }
    assert(ai_update_objects(&a, &s, &w, 10, 10) == 0);
    checkpoint();
    w.persons[1].unk_0018 = 1;
    assert(ai_update_objects(&a, &s, &w, 20, 20) == 0);
    checkpoint();
    platform_shutdown();
    return 0;
}
