#include "../src/ai/objects.c"
#include <assert.h>
#include <stdio.h>

static ai_state actors;
static sim_state simulation;
static world game;
static unsigned launches, moves, move_tick;

int __wrap_ai_launch_missile(ai_state *a, sim_state *s, world *w,
                            unsigned source, unsigned target, unsigned type)
{
    assert(a == &actors && s == &simulation && w == &game);
    assert(source == 2 && target == 3 && type == 20);
    ++launches;
    return 0;
}

int __wrap_ai_move_object(ai_state *a, sim_state *s, world *w,
                         unsigned person, int tick, int moving)
{
    assert(a == &actors && s == &simulation && w == &game);
    assert(person == 1 && moving == 0);
    ++moves; move_tick = (unsigned)tick;
    return 0;
}

int main(void)
{
    world_person *p = &game.persons[1];
    unsigned status, loop, bp, target, tick, seed, span, rows = 0;
    int cooldown, burst, scanned;
    actors.monster_count = 1;
    actors.monsters[0].kind = 14;
    actors.monsters[0].unk_007C = 7;
    actors.fire_missile[0] = 20;
    game.object_count = 4; game.person_count = 2;
    p->object = 2;
    while ((scanned = scanf("%u %d %d %u %u %u %u %u %u", &status, &cooldown,
            &burst, &loop, &bp, &target, &tick, &seed, &span)) == 9) {
        assert(status <= 2 && cooldown >= INT16_MIN && cooldown <= INT16_MAX);
        assert(burst >= INT16_MIN && burst <= INT16_MAX);
        assert(loop <= 1 && bp <= 1 && (target == 0 || target == 3));
        assert(tick <= 1 && seed <= UINT16_MAX && span <= INT16_MAX);
        launches = moves = move_tick = 0;
        p->state = 14; p->status = (int16_t)status; p->unk_00BC = 1;
        p->unk_0016 = 0x4321; p->unk_0018 = (int16_t)loop;
        p->unk_001E = (int16_t)cooldown; p->unk_0034 = (int16_t)burst;
        actors.monsters[0].unk_007E = (int16_t)span;
        simulation.bp_limit_flag = bp;
        game.rng.word[0] = 3000; game.rng.word[1] = (uint16_t)seed;
        game.rng.word[2] = 29999;
        assert(!actor_state(&actors, &simulation, &game, 1, (int)tick,
                            1, 0, 0, 0, 0, target, NULL));
        printf("%d %u %d %d %d %d %u %u %u %u %u %u\n", p->state,
            (unsigned)p->unk_00BC, p->unk_001E, p->unk_0034, p->unk_0016,
            p->unk_0018, game.rng.word[0], game.rng.word[1], game.rng.word[2],
            launches, moves, move_tick);
        ++rows;
    }
    assert(scanned == EOF && rows);
    return 0;
}
