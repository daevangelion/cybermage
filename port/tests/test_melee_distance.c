#include "ai.h"
#include "events.h"
#include "audio.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ai_state a;
static sim_state s;
static events_state e;
static world w;

/* Identical external boundaries are intercepted in melee_distance_original.py.
 * Visibility drops the second NPC's target; movement does not alter its pending
 * attack-completion flag. UpdateObjects and damage/RNG run normally. */
int __wrap_ai_sight_person(const ai_state *actors, sim_state *simulation,
                          world *game, unsigned source, unsigned target, int32_t range)
{
    (void)actors; (void)simulation; (void)target; (void)range;
    if (source == 2) game->persons[source].target = -1;
    return 0;
}
int __wrap_ai_move_object(ai_state *actors, sim_state *simulation, world *game,
                         unsigned person, int tick, int moving)
{
    (void)actors; (void)simulation; (void)tick; (void)moving;
    if (person == 1) game->persons[person].distance = -123;
    return 0;
}
void __wrap_ai_track_target(world *game, unsigned object, unsigned other, int offset)
{
    (void)game; (void)object; (void)other; (void)offset;
}
void __wrap_ai_update_pc_object(world *game, int tick) { (void)game; (void)tick; }

int main(int argc, char **argv)
{
    unsigned i;
    long prior, flash = 0;
    char *end;
    int no_prior;
    if (argc < 2 || argc > 4) return 2;
    no_prior = argc == 3;
    if (no_prior && strcmp(argv[2], "--no-prior-target")) return 2;
    if (argc == 4) {
        if (strcmp(argv[2], "--flash")) return 2;
        flash = strtol(argv[3], &end, 10);
        if (!*argv[3] || *end || flash < INT32_MIN || flash > INT32_MAX) return 2;
    }
    prior = strtol(argv[1], &end, 10);
    if (!*argv[1] || *end || prior < 0 || prior > INT32_MAX) return 2;
    assert(platform_init(1) == 0); snd_enable(0, 0);
    a.tables_loaded = 1; a.monster_count = 1; a.events = &e; a.difficulty = 1;
    a.undefined_distance_person = 99;
    a.pc_flash = (int32_t)flash;
    a.pc_sound = -1; s.ai = &a; s.events = &e; s.pc_mode = 5; s.bp_limit_flag = 1;
    w.scene = 1; w.person_count = 3; w.object_count = 4; w.player_object = 1;
    w.rng.word[0] = 17871; w.rng.word[1] = 21011; w.rng.word[2] = 26122;
    a.monsters[0].kind = 9; a.monsters[0].accuracy = 100;
    a.monsters[0].aggression = 100; a.monsters[0].range = 0x40000;
    a.monsters[0].melee_range = 49152; a.monsters[0].unk_0060 = 30720;
    a.monsters[0].unk_0066 = 12; a.monsters[0].unk_006A = 1;
    a.monsters[0].melee_damage = (ai_dice){1, 5, 1};
    for (i = 0; i < 3; ++i) {
        world_person *p = &w.persons[i];
        world_object *o = &w.objects[i+1];
        p->object = (int16_t)(i+1); p->target = 0;
        p->unk_0024 = 100; p->unk_0038 = (40 << 16) | 40;
        p->unk_004C = p->unk_0050 = -1;
        o->active = o->targetable = 1; o->person = (int16_t)i;
        o->x = 0x80000+(int32_t)i*0x1000; o->y = 0x80000;
        o->z = 0x1000; o->height = 0x8000;
    }
    w.persons[1].state = 1; w.persons[1].distance = (int32_t)prior;
    w.persons[1].unk_001E = 1;
    w.persons[2].state = 4; w.persons[2].distance = 15517;
    w.persons[2].unk_0018 = w.persons[2].unk_001A = w.persons[2].unk_0034 = 1;
    w.persons[2].unk_001E = 10; w.persons[2].unk_003C = 16;
    if (no_prior) w.objects[2].active = 0;
    assert(ai_update_objects(&a, &s, &w, 10, 10) == 0);
    if (no_prior) {
        assert(a.undefined_distance_person == 2);
        assert(a.undefined_distance_value == 15517);
    } else assert(!a.undefined_distance_person);
    printf("%d %u %u %u %" PRId32 " %d %d %d %d %" PRId32 " %u %" PRId32 "\n",
           (int16_t)w.persons[0].unk_0038,
           w.rng.word[0], w.rng.word[1], w.rng.word[2],
           w.persons[2].target, w.persons[2].state, w.persons[2].unk_001E,
           (int16_t)w.persons[2].unk_003C, (int16_t)w.persons[0].unk_003C,
           a.pc_flash, a.undefined_distance_person, a.undefined_distance_value);
    platform_shutdown();
    return 0;
}
