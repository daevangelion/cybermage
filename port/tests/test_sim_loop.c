#define main cybermage_main
#define snd_close sim_loop_checks
#include "../src/main.c"
#undef main
#undef snd_close
#include <assert.h>

void snd_close(void);
static const char *test_user;

/* Run while main still owns the loaded resources; no duplicated scene loader. */
void sim_loop_checks(void)
{
    world_object position = game.objects[game.player_object];
    assert(events.items[0].type == 120);
    assert(events.energy == 40 && events.cash == 85);
    assert(simulation.pc_mode == 5);
    assert((uint16_t)game.persons[0].unk_0038 == 40);
    assert((uint16_t)((uint32_t)game.persons[0].unk_0038 >> 16) == 40);
    events.cash = 123;
    events.energy = 31;
    game.persons[0].unk_0038 = 40 * 65536 + 23;
    assert(!save_game(test_user, 0));
    events.cash = 0;
    events.energy = 0;
    assert(!load_game("../orig/retail", test_user, 0, 10));
    assert(events.cash == 123 && events.energy == 31);
    assert((uint16_t)game.persons[0].unk_0038 == 23);
    assert(events.SAVELife == 23 && events.SAVELifeMax == 40);
    events.scene_requested.pending = 1;
    events.scene_requested.scene = 1;
    events.scene_requested.x = position.x;
    events.scene_requested.y = position.y;
    events.scene_requested.z = position.floor_z;
    events.scene_requested.heading = position.heading;
    assert(!next_scene("../orig/retail"));
    assert(!events.scene_requested.pending);
    assert(events.cash == 123 && events.energy == 31);
    assert((uint16_t)game.persons[0].unk_0038 == 23);
    assert(game.objects[game.player_object].x == position.x);
    assert(game.objects[game.player_object].y == position.y);
    snd_close();
}

int main(int argc, char **args)
{
    const char *argv[] = {"cybermage", "--headless", "--data", "../orig/retail",
        "--user", NULL, "--keys", "../tools/scenarios/port-movement.keys",
        "--frames", "4", "--bios-seed", "44"};
    assert(argc == 2);
    test_user = args[1];
    argv[5] = test_user;
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    return 0;
}
