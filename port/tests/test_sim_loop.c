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
    memset(ui.auto_slot, 0, sizeof ui.auto_slot);
    memset(events.auto_slot, 0, sizeof events.auto_slot);
    events.cash = 123;
    events.energy = 31;
    game.persons[0].unk_0038 = 40 * 65536 + 23;
    game.persons[1].status = 0;
    game.persons[1].unk_0038 = 12 * 65536 + 5;
    assert(!ai_set_difficulty(&actors, &game, 2));
    ui.options[8] = 2;
    assert(!save_game(test_user, 0));
    assert(actors.difficulty == 2);
    events.cash = 0;
    events.energy = 0;
    assert(!load_game("../orig/retail", test_user, 0, 10));
    assert(events.cash == 123 && events.energy == 31);
    assert((uint16_t)game.persons[0].unk_0038 == 23);
    assert(events.SAVELife == 23 && events.SAVELifeMax == 40);
    assert(actors.difficulty == 2);
    assert((uint16_t)game.persons[1].unk_0038 == 11);
    assert((uint16_t)((uint32_t)game.persons[1].unk_0038 >> 16) == 25);
    assert(!ai_set_difficulty(&actors, &game, 1));
    ui.options[8] = 1;
    events.scene_requested.pending = 1;
    events.scene_requested.scene = 1;
    events.scene_requested.x = position.x;
    events.scene_requested.y = position.y;
    events.scene_requested.z = position.floor_z;
    events.scene_requested.heading = position.heading;
    assert(!next_scene("../orig/retail", test_user));
    assert(!events.scene_requested.pending);
    assert(events.cash == 123 && events.energy == 31);
    assert((uint16_t)game.persons[0].unk_0038 == 23);
    assert(game.objects[game.player_object].x == position.x);
    assert(game.objects[game.player_object].y == position.y);
    events.events[1].value[0] = 765;
    events.scene_requested.pending = 1;
    events.scene_requested.auto_mode = 2;
    assert(!next_scene("../orig/retail", test_user));
    assert(auto_save == 0);
    events.cash = 124;
    events.energy = 30;
    game.persons[0].unk_0038 = 40 * 65536 + 17;
    events.scene_requested.pending = 1;
    events.scene_requested.auto_mode = 1;
    assert(!next_scene("../orig/retail", test_user));
    assert(auto_save == -1);
    assert(events.events[1].value[0] == 765);
    assert(events.cash == 124 && events.energy == 30);
    assert((uint16_t)game.persons[0].unk_0038 == 17);
    assert(!death_start(10));
    {
        cm_input in = {0};
        assert(!death_step(&in, 69, 0) && death_phase == 1);
        assert(!death_step(&in, 70, 0) && death_phase >= 2);
        in.pressed[1] = 1;
        assert(!death_step(&in, 71, 0));
        assert(death_phase == 3 && ui.screen == UI_DEATH);
    }
    {
        unsigned slot;
        for (slot = 0; slot < EVENTS_ITEM_MAX && events.items[slot].type > 0; ++slot) {}
        assert(slot + 1 < EVENTS_ITEM_MAX);
        assert(events_give_key(&events, 26, 100) == 1);
        assert(events_give_key(&events, 26, 101) == 1);
        assert(events.items[slot].type == 26 && events.items[slot].subtype == 100);
        assert(events.items[slot + 1].type == 26 && events.items[slot + 1].subtype == 101);
        assert(!events.items[slot].quantity && !events.items[slot + 1].quantity);
        for (slot = 0; slot < EVENTS_ITEM_MAX; ++slot) events.items[slot].type = 1;
        assert(events_give_key(&events, 26, 100) == -1);
    }
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
