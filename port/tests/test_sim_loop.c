#define main cybermage_main
#define snd_close sim_loop_checks
#define platform_poll loop_poll
#include "../src/main.c"
#undef main
#undef snd_close
#undef platform_poll
#include <assert.h>

void snd_close(void);
int platform_poll(cm_input *input, uint32_t frame);
static const char *test_user;
static int training, generic_hand, vehicle_death;

int loop_poll(cm_input *input, uint32_t frame)
{
    if (vehicle_death && frame == 3) {
        game.player_person = 1;
        game.persons[1].status = 2;
        events.pc_dead_man = 0;
    }
    return platform_poll(input, frame);
}

static void dirty_selectors(void)
{
    unsigned i;
    for(i=0;i<3;++i) {
        events.selectors[i].count=2;
        events.selectors[i].index=1;
    }
}

static void selectors_reset(void)
{
    unsigned i;
    for(i=0;i<3;++i)
        assert(!events.selectors[i].count && events.selectors[i].index==UINT32_MAX);
}

static void selector_key(unsigned scan)
{
    cm_input key={0};
    uint32_t ticks=simulation.timer_ticks+2;
    key.pressed[scan]=1;
    key.raw_count=2;key.raw_scans[0]=(uint8_t)scan;key.raw_scans[1]=(uint8_t)(scan|0x80);
    assert(menu_input(&key,ticks)==UI_NONE && ui.screen==UI_GAME);
    assert(!sim_frame(&simulation,&game,&key,ticks));
    assert(!ai_pc_input(&actors,&simulation,&game));
    assert(!events_step(&events,&simulation,&actors,&game));
}

/* Run while main still owns the loaded resources; no duplicated scene loader. */
void sim_loop_checks(void)
{
    frame_draw draw = {0};
    world_object position = game.objects[game.player_object];
    if (vehicle_death) {
        assert(!death_phase && !events.pc_dead_man);
        game.player_person = 0;
        snd_close();
        return;
    }
    if(generic_hand) {
        assert(game.scene==1 && simulation.pc_mode==3 && !events.selector_kind);
        assert(!events_selector_needs_input(&events));
        snd_close();
        return;
    }
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
    dirty_selectors();
    assert(!save_game(test_user, 0, &draw));
    selectors_reset();
    assert(actors.difficulty == 2);
    events.cash = 0;
    events.energy = 0;
    simulation.kb_latch = 12;
    simulation.bios_shift = 3;
    actors.pc.sp_regen_timer = 123;
    ui.restart_game = 10;
    dirty_selectors();
    {
        uint16_t before[3];
        uint32_t animation = actors.animation_deadline;
        uint32_t fast = actors.fast_animation_deadline;
        memcpy(before, game.rng.word, sizeof before);
        assert(!load_game("../orig/retail", test_user, 0, 10));
        assert(!memcmp(before, game.rng.word, sizeof before));
        assert(actors.animation_deadline == animation && actors.fast_animation_deadline == fast);
    }
    selectors_reset();
    assert(simulation.kb_latch == 12 && simulation.bios_shift == 3);
    assert(actors.pc.sp_regen_timer == 123);
    assert(ui.restart_game == 0);
    assert(events.cash == 123 && events.energy == 31);
    assert((uint16_t)game.persons[0].unk_0038 == 23);
    {
        cm_input key={0};
        unsigned i;
        simulation.kb_latch=3;
        key.pressed[0x1c]=key.held[0x1c]=1;
        key.raw_count=1;key.raw_scans[0]=0x1c;
        ui_open(&ui,UI_LOAD);ui.return_screen=UI_GAME;ui.selected=0;
        sim_input(&simulation,&key);
        assert(simulation.kb_scan_status[0x1c]==3);
        assert(menu_input(&key,11)==UI_LOAD_GAME);
        assert(!key.raw_count && !key.held[0x1c] && !key.pressed[0x1c]);
        for(i=0;i<SIM_SCAN_COUNT;++i) assert(!simulation.kb_scan_status[i]);
        assert(load_requested("../orig/retail",test_user,11)==1);
        sim_input(&simulation,&key);
        assert(!simulation.kb_scan_status[0x1c]);
        key.pressed[0x1c]=key.held[0x1c]=1;
        key.raw_count=1;key.raw_scans[0]=0x1c;
        assert(menu_input(&key,12)==UI_NONE);
        assert(key.raw_count==1 && key.pressed[0x1c]);
        sim_input(&simulation,&key);
        assert(simulation.kb_scan_status[0x1c]==3);
        simulation.kb_scan_status[0x1c]=0;
    }
    {
        cm_input key={0};
        ui_open(&ui,UI_GAME);
        assert(!events.items[1].type && events_inventory_add(&events,4,1)==1);
        selector_key(0x3B); /* Real menu_input must not turn F1 into Help. */
        assert(events.selector_kind==EVENTS_SELECT_WEAPON && simulation.pc_mode==3);
        selector_key(1);
        assert(!events.selector_kind && simulation.pc_mode==5 && ui.screen==UI_GAME);
        selector_key(0x3D);
        assert(events.selector_kind==EVENTS_SELECT_KEY && simulation.pc_mode==3);
        assert(events.selectors[EVENTS_SELECT_KEY-1].count==1);
        selector_key(0x3F); /* Unmapped F5/F7 stay selector input, not file menus. */
        assert(events.selector_kind==EVENTS_SELECT_KEY && ui.screen==UI_GAME);
        selector_key(0x41);
        assert(events.selector_kind==EVENTS_SELECT_KEY && ui.screen==UI_GAME);
        key.quit=1;
        assert(menu_input(&key,10)==UI_QUIT);
        selector_key(1);
        assert(!events.selector_kind && simulation.pc_mode==5 && ui.screen==UI_GAME);
        selector_key(0x3C);
        assert(events.selector_kind==EVENTS_SELECT_POWER && simulation.pc_mode==3);
        selector_key(1);
        assert(!events.selector_kind && simulation.pc_mode==5);
        assert(events_inventory_remove(&events,1,1)==1);
        key.quit=0;key.pressed[0x3F]=1;
        assert(menu_input(&key,10)==UI_NONE && ui.screen==UI_SAVE);
        ui_open(&ui,UI_GAME);
        key.pressed[0x3F]=0;key.pressed[0x41]=1;
        assert(menu_input(&key,10)==UI_NONE && ui.screen==UI_LOAD);
        ui_open(&ui,UI_GAME);
        key.pressed[0x41]=0;key.pressed[53]=1;
        assert(menu_input(&key,10)==UI_NONE && ui.screen==UI_HELP);
        ui_open(&ui,UI_GAME);
    }
    assert(events.SAVELife == 23 && events.SAVELifeMax == 40);
    assert(actors.difficulty == 2);
    assert((uint16_t)game.persons[1].unk_0038 == 11);
    assert((uint16_t)((uint32_t)game.persons[1].unk_0038 >> 16) == 25);
    {
        cm_input key = {0};
        int32_t hp = game.persons[1].unk_0038;
        ui_open(&ui, UI_OPTIONS);
        ui.options[8] = 0;
        assert(menu_input(&key, 12) == UI_NONE);
        assert(actors.difficulty == 2 && game.persons[1].unk_0038 == hp);
        ui.options[8] = 1;
        key.pressed[1] = 1;
        assert(menu_input(&key, 13) == UI_NONE);
        assert(actors.difficulty == 1 && actors.attack_difficulty == 1);
        ui_open(&ui, UI_GAME);
    }
    assert(!ai_set_difficulty(&actors, &game, 1));
    ui.options[8] = 1;
    events.scene_requested.pending = 1;
    events.scene_requested.scene = 1;
    events.scene_requested.x = position.x;
    events.scene_requested.y = position.y;
    events.scene_requested.z = position.floor_z;
    events.scene_requested.heading = position.heading;
    assert(!next_scene("../orig/retail", test_user, &draw));
    assert(!events.scene_requested.pending);
    assert(events.cash == 123 && events.energy == 31);
    assert((uint16_t)game.persons[0].unk_0038 == 23);
    assert(game.objects[game.player_object].x == position.x);
    assert(game.objects[game.player_object].y == position.y);
    events.events[1].value[0] = 765;
    events.scene_requested.pending = 1;
    events.scene_requested.auto_mode = 2;
    assert(!next_scene("../orig/retail", test_user, &draw));
    assert(auto_save == 0);
    events.cash = 124;
    events.energy = 30;
    game.persons[0].unk_0038 = 40 * 65536 + 17;
    events.scene_requested.pending = 1;
    events.scene_requested.auto_mode = 1;
    assert(!next_scene("../orig/retail", test_user, &draw));
    assert(auto_save == -1);
    assert(events.events[1].value[0] == 765);
    assert(events.cash == 124 && events.energy == 30);
    assert((uint16_t)game.persons[0].unk_0038 == 17);
    {
        cm_input pause = {0};
        simulation.swim_loop = 0; /* Handle-state fixture, independent of mute/device. */
        actors.pc.sword_hum_slot = 0;
        actors.pc.sword_hum_sound = 61;
        ui_open(&ui, UI_GAME);
        pause.pressed[1] = 1;
        assert(menu_input(&pause, 9) == UI_NONE);
        assert(ui.screen == UI_PAUSE && simulation.swim_loop == -1);
        assert(actors.pc.sword_hum_slot == 0 && actors.pc.sword_hum_sound == 61);
    }
    simulation.swim_loop = 0;
    actors.pc_sound = 0;
    actors.pc.loop_sound = 0;
    actors.pc.sword_hum_slot = 0;
    actors.pc.sword_hum_sound = 61;
    draw.frame = 10;
    assert(!death_start(&draw));
    assert(simulation.swim_loop == -1 && actors.pc_sound == -1);
    assert(actors.pc.loop_sound == -1);
    assert(actors.pc.sword_hum_slot == -1 && actors.pc.sword_hum_sound == 61);
    {
        cm_input in = {0};
        ui_view view;
        assert(!ui_game_view(&ui, simulation.pc_mode, simulation.vehicle_type, &view));
        assert(!render_set_view(view.x, view.y, view.width, view.height));
        draw.frame = 69;
        assert(!death_step(&draw, 0) && death_phase == 1);
        draw.frame = 70;
        assert(!death_step(&draw, 0) && death_phase >= 2);
        in.pressed[1] = 1;
        sim_scan(&simulation, 1);
        draw.frame = 71;
        assert(!death_step(&draw, 0));
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
            assert(!save_game(test_user, 0, &draw));
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
        events.pc_dead_man = 1;
        assert(load_requested("../orig/retail", test_user, 75) == -1);
        assert(ui.screen == UI_LOAD && ui.return_screen == UI_TITLE);
        assert(!game.object_count && !events.pc_dead_man);
        events.pc_dead_man = 1;
        assert(!load_game("../orig/retail", test_user, 0, 76));
        assert(!events.pc_dead_man);
    }
    {
        unsigned kind;
        for (kind = SND_MOVIE_CREDITS; kind <= SND_MOVIE_ENDING; ++kind) {
            actors.pc.sword_hum_slot = 0;
            actors.pc.sword_hum_sound = 61;
            assert(!movie_start("../orig/retail", kind, 0));
            assert(actors.pc.sword_hum_slot == -1 && actors.pc.sword_hum_sound == 61);
            assert(snd_movie_active());
            snd_movie_end();
        }
    }
    actors.pc.sword_hum_slot = 0;
    actors.pc.sword_hum_sound = 61;
    snd_close();
}

int main(int argc, char **args)
{
    static const uint8_t training_keys[] =
        "0 raw 9c\n1 down e050\n1 up e050\n2 down 1c\n2 up 1c\n";
    static const uint8_t generic_keys[] =
        "0 raw 9c\n1 down 1c\n1 up 1c\n3 down 1d\n";
    char keys[PATH_BYTES];
    const char *argv[] = {"cybermage", "--headless", "--data", "../orig/retail",
        "--user", NULL, "--keys", "../tools/scenarios/port-movement.keys",
        "--frames", "4", "--bios-seed", "44"};
    assert(argc == 2);
    test_user = args[1];
    argv[5] = test_user;
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    assert(actors.pc.sword_hum_slot == -1 && actors.pc.sword_hum_sound == 61);
    vehicle_death = 1;
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    vehicle_death = 0;
    assert(!make_path(keys, sizeof keys, test_user, "training.keys"));
    assert(!platform_file_write(keys, training_keys, sizeof training_keys - 1));
    argv[7] = keys;
    training = 1;
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    assert(!make_path(keys,sizeof keys,test_user,"generic-hand.keys"));
    assert(!platform_file_write(keys,generic_keys,sizeof generic_keys-1));
    training=0;generic_hand=1;
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    generic_hand=0;
    {
        const char *bad_slot[] = {"cybermage", "--replay-load-slot", "11"};
        const char *missing_user[] = {"cybermage", "--replay", "frames.csv",
                                      "--replay-load-slot", "0"};
        const char *snapshot_without_trace[] = {"cybermage", "--import-undefined", "persons"};
        assert(cybermage_main(3, (char **)bad_slot) == 2);
        assert(cybermage_main(5, (char **)missing_user) == 2);
        assert(cybermage_main(3, (char **)snapshot_without_trace) == 2);
    }
    {
        const char *bad_renderer[] = {"cybermage", "--renderer", "invalid"};
        gl_enabled = gl_recording = gl_overlay_valid = 1;
        gl_width = 640; gl_height = 480;
        assert(cybermage_main(3, (char **)bad_renderer) == 2);
        assert(!gl_enabled && !gl_recording && !gl_overlay_valid);
        assert(!gl_width && !gl_height);
    }
    return 0;
}
