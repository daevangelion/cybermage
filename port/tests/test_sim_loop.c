#define main cybermage_main
#define snd_close sim_loop_checks
#include "../src/main.c"
#undef main
#undef snd_close
#include <assert.h>

void snd_close(void);
static const char *test_user;
static int training;

/* Run while main still owns the loaded resources; no duplicated scene loader. */
void sim_loop_checks(void)
{
    world_object position = game.objects[game.player_object];
    if (training) {
        assert(game.scene == 11);
        assert(events.energy == 100 && events.cash == 60);
        assert((uint16_t)game.persons[0].unk_0038 == 100);
        snd_close();
        return;
    }
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
    simulation.kb_latch = 12;
    simulation.bios_shift = 3;
    actors.pc.sp_regen_timer = 123;
    ui.restart_game = 10;
    assert(!load_game("../orig/retail", test_user, 0, 10));
    assert(simulation.kb_latch == 12 && simulation.bios_shift == 3);
    assert(actors.pc.sp_regen_timer == 123);
    assert(ui.restart_game == 0);
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
    {
        cm_input pause = {0};
        simulation.swim_loop = 0; /* Handle-state fixture, independent of mute/device. */
        ui_open(&ui, UI_GAME);
        pause.pressed[1] = 1;
        assert(menu_input(&pause, 9) == UI_NONE);
        assert(ui.screen == UI_PAUSE && simulation.swim_loop == -1);
    }
    simulation.swim_loop = 0;
    actors.pc_sound = 0;
    actors.pc.loop_sound = 0;
    assert(!death_start(10));
    assert(simulation.swim_loop == -1 && actors.pc_sound == -1);
    assert(actors.pc.loop_sound == -1);
    {
        cm_input in = {0};
        assert(!death_step(&in, 69, 0) && death_phase == 1);
        assert(!death_step(&in, 70, 0) && death_phase >= 2);
        in.pressed[1] = 1;
        assert(!death_step(&in, 71, 0));
        assert(death_phase == 3 && ui.screen == UI_DEATH);
        ui.restart_game = 10;
        ui.slot = 0;
        in.pressed[1] = 0;
        in.pressed[0x1c] = 1;
        assert(ui_input(&ui, &in, 72) == UI_LOAD_GAME && ui.slot == 10);
        ui_open(&ui, UI_LOAD);
        ui.selected = 7;
        ui.return_screen = UI_DEATH;
        assert(!load_requested("../orig/retail", "/missing/cybermage-test-save", 73));
        assert(ui.screen == UI_LOAD && ui.selected == 7 && ui.return_screen == UI_DEATH);
        {
            int before = auto_save;
            int16_t pointer = ui.auto_ptr[1];
            ui.slot = 1;
            ui.auto_ptr[1] = 20;
            assert(!load_requested("../orig/retail", test_user, 73));
            assert(auto_save == before);
            ui.auto_ptr[1] = pointer;
            assert(!save_game(test_user, 0));
        }
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
    {
        char path[PATH_BYTES];
        uint32_t size;
        unsigned objects = game.object_count;
        assert(!encode_save(&size));
        assert(!make_path(path, sizeof path, test_user, "SAVE001.DAT"));
        memset(save_data + 0x714, 0xff, 4);
        save_data[0x717] = 0x7f;
        assert(!platform_file_write(path, save_data, size));
        ui_open(&ui, UI_LOAD);
        ui.slot = 1;
        assert(!load_requested("../orig/retail", test_user, 74));
        assert(game.object_count == objects && ui.screen == UI_LOAD);
        assert(cm_last_error()->offset == 0x714);
        assert(!encode_save(&size));
        save_data[UI_SAVE_PREFIX] = save_data[UI_SAVE_PREFIX + 1] = 0xff;
        assert(!platform_file_write(path, save_data, size));
        assert(load_requested("../orig/retail", test_user, 75) == -1);
        assert(ui.screen == UI_LOAD && ui.return_screen == UI_TITLE);
        assert(!game.object_count);
    }
    snd_close();
}

int main(int argc, char **args)
{
    static const uint8_t training_keys[] =
        "0 raw 9c\n1 down e050\n1 up e050\n2 down 1c\n2 up 1c\n";
    char keys[PATH_BYTES];
    const char *argv[] = {"cybermage", "--headless", "--data", "../orig/retail",
        "--user", NULL, "--keys", "../tools/scenarios/port-movement.keys",
        "--frames", "4", "--bios-seed", "44"};
    assert(argc == 2);
    test_user = args[1];
    argv[5] = test_user;
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    assert(!make_path(keys, sizeof keys, test_user, "training.keys"));
    assert(!platform_file_write(keys, training_keys, sizeof training_keys - 1));
    argv[7] = keys;
    training = 1;
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    {
        const char *bad_slot[] = {"cybermage", "--replay-load-slot", "11"};
        const char *missing_user[] = {"cybermage", "--replay", "frames.csv",
                                      "--replay-load-slot", "0"};
        const char *snapshot_without_trace[] = {"cybermage", "--import-undefined", "persons"};
        assert(cybermage_main(3, (char **)bad_slot) == 2);
        assert(cybermage_main(5, (char **)missing_user) == 2);
        assert(cybermage_main(3, (char **)snapshot_without_trace) == 2);
    }
    return 0;
}
