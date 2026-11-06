#include "ai.h"
#include "audio.h"
#include "controls.h"
#include "events.h"
#include "gfx.h"
#include "platform.h"
#include "render.h"
#include "replay.h"
#include "rsc.h"
#include "sim.h"
#include "tgv.h"
#include "ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_BYTES 1024
#define STATE_TEXT_BYTES 2048

static uint8_t pixels[CM_WIDTH * CM_HEIGHT], palette[768];
static uint8_t base_palette[768];
static uint8_t save_data[UI_SAVE_MAX];
static uint8_t auto_data[UI_SAVE_MAX];
static uint8_t death_background[CM_WIDTH * CM_HEIGHT];
static unsigned death_columns[CM_WIDTH];
static uint32_t death_deadline;
static int death_phase;
static world game;
static ai_state actors;
static events_state events;
static sim_state simulation;
static cm_ui ui;
static ui_save_state save;
static rsc hipak;
static cm_file strings;
static cm_file executable;
static cm_exe exe;
static cm_controls controls;
static cm_replay replay;
static int16_t headless_pcm[48000 / CM_TICK_HZ * 2];
static char resource_path[PATH_BYTES], strings_path[PATH_BYTES], save_path[PATH_BYTES];
static char names_path[PATH_BYTES];
static char executable_path[PATH_BYTES];
static char controls_path[PATH_BYTES];
static int auto_save = -1;

static int make_path(char *out, size_t size, const char *dir, const char *file)
{
    int n = snprintf(out, size, "%s/%s", dir, file);
    if (n < 0 || (size_t)n >= size) return cm_fail(dir, 0, "asset path too long");
    return 0;
}

static int number(const char *text, unsigned *value)
{
    unsigned long v;
    char *end;
    if (!*text || *text == '-') return -1;
    v = strtoul(text, &end, 10);
    if (*end || v > 1000000) return -1;
    *value = (unsigned)v;
    return 0;
}

static int game_names_write(void)
{
    uint8_t data[288];
    if (ui_game_names_write(&ui, data)) return -1;
    return platform_file_write(names_path, data, sizeof data);
}

static int game_names_read(const char *directory)
{
    cm_file file = {0};
    int exists, result;
    if (make_path(names_path, sizeof names_path, directory, "GAMENAME.USR")) return -1;
    exists = platform_file_exists(names_path);
    if (exists < 0) return -1;
    if (!exists) return game_names_write();
    if (platform_file_read(&file, names_path)) return -1;
    result = ui_game_names_read(&ui, file.data, file.size);
    platform_file_release(&file);
    return result;
}

static void camera_follow(void)
{
    const world_object *o = &game.objects[game.player_object];
    /* Image1::Render 065D34: renderer performs CameraInit's 256-unit
     * quantization. Eye offset is view +4B170, not extra object translation. */
    game.camera.u = o->x;
    game.camera.v = o->y;
    game.camera.z = o->floor_z + simulation.body_height - 0x1000 + simulation.unk_144b38;
    game.camera.angle = o->heading;
}

static int load_scene(const char *data, unsigned scene)
{
    world_close(&game);
    render_unload();
    memset(&actors, 0, sizeof actors);
    simulation.ai = &actors;
    simulation.events = &events;
    if (world_load_scene(&game, data, scene) || ai_init(&actors, &game) ||
        ai_set_difficulty(&actors, &game, (unsigned)ui.options[8])) return -1;
    ai_restore_pc_info(&events, &game);
    if (ai_load_tables(&actors, data) ||
        world_finalize_scene(&game) || sim_start_scene(&simulation, &game) ||
        events_init(&events, &game) || events_load_tables(&events, data) ||
        render_load(&game, &hipak) || snd_scene(&game)) return -1;
    actors.events = &events;
    ai_start_clock(&actors, simulation.previous_ticks);
    events.actors = &actors;
    events.ticks = simulation.timer_ticks;
    ui.shop_kind = 0;
    death_phase = 0;
    ui_save_init(&save, &game);
    ui_open(&ui, UI_GAME);
    return 0;
}

static int new_game(const char *data, unsigned scene, uint32_t ticks)
{
    uint8_t latch = simulation.kb_latch, modifiers = simulation.bios_shift;
    memset(&events, 0, sizeof events);
    auto_save = -1;
    sim_init(&simulation, ticks);
    simulation.kb_latch = latch;
    simulation.bios_shift = modifiers;
    if (ai_init_pc(&events, &simulation, &game.rng, &exe, scene, ticks)) return -1;
    memcpy(events.auto_ptr, ui.auto_ptr, sizeof events.auto_ptr);
    memcpy(events.auto_slot, ui.auto_slot, sizeof events.auto_slot);
    return load_scene(data, scene);
}

static int encode_save(uint32_t *size)
{
    unsigned difficulty = (unsigned)actors.difficulty;
    int result;
    if (ai_set_difficulty(&actors, &game, 1)) return -1;
    sim_save_export(&simulation, save.unk_6D4);
    save.FireCheck = (uint32_t)simulation.fire_check;
    save.FireDamage = (uint32_t)simulation.fire_damage;
    save.FireTilePanel = (uint32_t)simulation.fire_tile_panel;
    save.FireTileArt = (uint32_t)simulation.fire_tile_art;
    ai_save_export(&actors, &save);
    result = events_save_export(&events, &save) ||
        ui_save_game(&save, &game, save_data, sizeof save_data, size);
    if (ai_set_difficulty(&actors, &game, difficulty)) return -1;
    return result ? -1 : 0;
}

static int save_game(const char *user, unsigned slot)
{
    char name[24];
    uint32_t size;
    if (slot > UI_SAVE_SLOTS) return cm_fail("SaveGame", slot, "invalid save slot");
    snprintf(name, sizeof name, "SAVE%03u.DAT", slot);
    if (make_path(save_path, sizeof save_path, user, name) || encode_save(&size)) return -1;
    if (platform_file_write(save_path, save_data, size)) return -1;
    ui.restart_game = (int32_t)slot;
    if (slot == UI_SAVE_SLOTS) ui.restart_auto = auto_save;
    else ui.auto_ptr[slot] = (int16_t)auto_save;
    if (auto_save >= 0) ui.auto_slot[auto_save] = 1;
    memcpy(events.auto_ptr, ui.auto_ptr, sizeof events.auto_ptr);
    memcpy(events.auto_slot, ui.auto_slot, sizeof events.auto_slot);
    return game_names_write();
}

static int load_game(const char *data, const char *user, unsigned slot, uint32_t ticks)
{
    char name[24];
    cm_file file = {0};
    int result;
    unsigned scene;
    if (slot > UI_SAVE_SLOTS) return cm_fail("LoadGame", slot, "invalid save slot");
    auto_save = slot == UI_SAVE_SLOTS ? ui.restart_auto : ui.auto_ptr[slot];
    if (auto_save < -1 || auto_save >= EVENTS_AUTO_MAX)
        return cm_fail("GAMENAME.USR", slot, "invalid automatic save reference");
    snprintf(name, sizeof name, "SAVE%03u.DAT", slot);
    if (make_path(save_path, sizeof save_path, user, name) ||
        platform_file_read(&file, save_path)) return -1;
    if (ui_save_scene(file.data, file.size, &scene)) {
        platform_file_release(&file);
        return -1;
    }
    sim_init(&simulation, ticks);
    result = load_scene(data, scene) ||
        ui_save_overlay(&save, &game, file.data, file.size);
    platform_file_release(&file);
    if (result) return -1;
    if (sim_save_import(&simulation, save.unk_6D4)) return -1;
    simulation.fire_check = (int32_t)save.FireCheck;
    simulation.fire_damage = (int32_t)save.FireDamage;
    simulation.fire_tile_panel = (int32_t)save.FireTilePanel;
    simulation.fire_tile_art = (int32_t)save.FireTileArt;
    actors.difficulty = 1;
    events.ticks = ticks;
    if (ai_save_import(&actors, &game, &save) || events_save_import(&events, &save))
        return -1;
    actors.events = &events;
    ai_start_clock(&actors, ticks);
    events.actors = &actors;
    memcpy(events.auto_ptr, ui.auto_ptr, sizeof events.auto_ptr);
    memcpy(events.auto_slot, ui.auto_slot, sizeof events.auto_slot);
    if (ai_set_difficulty(&actors, &game, (unsigned)ui.options[8])) return -1;
    ai_save_pc_info(&events, &game);
    ui.shop_kind = 0;
    death_phase = 0;
    camera_follow();
    return 0;
}

static int save_auto(const char *user, unsigned slot)
{
    char name[24];
    uint32_t size, length;
    snprintf(name, sizeof name, "AUTO%03u.DAT", slot);
    if (make_path(save_path, sizeof save_path, user, name) || encode_save(&size) ||
        sim_auto_pack(save_data, size, auto_data, sizeof auto_data, &length)) return -1;
    return platform_file_write(save_path, auto_data, length);
}

/* AutoLoad 051EA0..051F61: scene9 return keys use subtype overrides. */
static int auto_return(unsigned previous, unsigned restored)
{
    static const unsigned keys[5][2] = {
        {26, 100}, {8, 101}, {104, 0}, {10, 103}, {27, 102}
    };
    unsigned i, count;
    if (restored != 9 || (previous != 4 && previous != 5)) return 0;
    if (events.event_count < (previous == 4 ? 263u : 313u))
        return cm_fail("AutoLoad", 9, "missing return-scene events");
    if (previous == 4) {
        events.events[238].enabled = events.events[263].enabled = 1;
        count = 3;
    } else {
        events.events[274].enabled = events.events[276].enabled = 1;
        events.events[264].enabled = events.events[249].enabled = 0;
        events.events[266].enabled = 0;
        events.events[149].enabled = events.events[313].enabled = 1;
        count = 5;
    }
    for (i = 0; i < count; ++i)
        if (events_give_key(&events, keys[i][0], (int32_t)keys[i][1]) < 0) return -1;
    return 0;
}

static int load_auto(const char *data, const char *user, unsigned slot)
{
    char name[24];
    cm_file file = {0};
    uint32_t size;
    unsigned i, scene, previous = game.scene;
    int result = -1;
    snprintf(name, sizeof name, "AUTO%03u.DAT", slot);
    if (make_path(save_path, sizeof save_path, user, name) ||
        platform_file_read(&file, save_path)) return -1;
    if (file.size < SIM_AUTO_PREFIX || (scene = cm_u32(file.data)) >= 12) {
        cm_fail(save_path, 0, "invalid auto save scene or length");
        goto out;
    }
    if (load_scene(data, scene) || encode_save(&size) ||
        sim_auto_unpack(save_data, size, file.data, file.size) ||
        ui_save_overlay(&save, &game, save_data, size) ||
        events_save_import(&events, &save)) goto out;
    actors.difficulty = 1;
    if (ai_set_difficulty(&actors, &game, (unsigned)ui.options[8])) goto out;
    ai_restore_pc_info(&events, &game);
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        actors.missiles[i].active = 0;
        game.objects[actors.missiles[i].object].active = 0;
    }
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        actors.souls[i].active = 0;
        game.objects[actors.souls[i].object].active = 0;
    }
    simulation.fire_check = (int32_t)save.FireCheck;
    simulation.fire_damage = (int32_t)save.FireDamage;
    simulation.fire_tile_panel = (int32_t)save.FireTilePanel;
    simulation.fire_tile_art = (int32_t)save.FireTileArt;
    if (auto_return(previous, scene)) goto out;
    result = 0;
out:
    platform_file_release(&file);
    return result;
}

static int next_scene(const char *data, const char *user)
{
    events_scene_request request = events.scene_requested;
    world_object *player;
    events.scene_requested.pending = 0;
    if (request.scene < 0 || request.scene >= 12 || request.auto_mode < 0 || request.auto_mode > 2)
        return cm_fail("MoveWorld/scene", 0, "invalid destination scene");
    ai_save_pc_info(&events, &game);
    if (request.auto_mode == 2) {
        auto_save = events_find_auto_slot(&events);
        if (auto_save < 0) return cm_fail("AutoSave", 0, "all automatic save slots are occupied");
        if (save_auto(user, (unsigned)auto_save)) return -1;
    }
    if (request.auto_mode == 1 && auto_save >= 0) {
        if (load_auto(data, user, (unsigned)auto_save)) return -1;
        auto_save = -1;
    } else if (load_scene(data, (unsigned)request.scene)) return -1;
    player = &game.objects[game.player_object];
    world_omap_delete(&game, game.player_object);
    player->x = request.x;
    player->y = request.y;
    player->floor_z = request.z;
    player->heading = request.heading;
    if (world_make_omap(&game)) return -1;
    camera_follow();
    return save_game(user, UI_SAVE_SLOTS);
}

static int death_start(uint32_t ticks)
{
    unsigned x;
    if (snd_kill_loops(&game, 1)) return -1;
    for (x = 0; x < CM_WIDTH; ++x) death_columns[x] = 7;
    death_deadline = ticks + 60;
    death_phase = 1;
    return 0;
}

static int death_step(const cm_input *input, uint32_t ticks, int no_sound)
{
    int song, finished;
    if (death_phase == 1) {
        const uint8_t *image;
        uint32_t size;
        unsigned x, y;
        if (render_draw(&game, &game.camera, pixels)) return -1;
        if ((int32_t)(ticks - death_deadline) < 0) return 0;
        if (simulation.pc_mode == 7 &&
            events_set_tank_mode(&events, &simulation, &game, 0)) return -1;
        if (rsc_entry(&hipak, 1, &image, &size)) return -1;
        if (size != 320u * 240u)
            return cm_fail(resource_path, 1, "invalid death backdrop dimensions");
        for (y = 0; y < CM_HEIGHT; ++y)
            for (x = 0; x < CM_WIDTH; ++x)
                death_background[y * CM_WIDTH + x] = image[y / 2 * 320 + x / 2];
        death_phase = 2;
    }
    finished = ui_pc_death(pixels, death_background, death_columns, &game.rng);
    if (finished < 0) return -1;
    song = snd_check_song();
    if (song < 0) return -1;
    if (song || input->pressed[1] || (no_sound && finished)) {
        death_phase = 3;
        ui_open(&ui, UI_DEATH);
    }
    return 0;
}

static int state_dump(const char *directory, unsigned frame)
{
    char text[STATE_TEXT_BYTES];
    const world_object *o = &game.objects[game.player_object];
    const world_person *pc = &game.persons[game.player_person];
    unsigned i;
    int n = snprintf(text, sizeof text,
        "frame=%u ticks=%" PRIu32 " delta=%" PRId32 " x=%" PRId32 " y=%" PRId32
        " z=%" PRId32 " heading=%u pitch=%" PRId32
        " e8=%" PRId32 " ec=%" PRId32 " f0=%" PRId32 " f4=%" PRId32
        " f8=%" PRId32 " fc=%" PRId32 " b00=%" PRId32
        " b1c=%" PRId32 " b24=%" PRId32 " b28=%" PRId32
        " b2c=%" PRId32 " b34=%" PRId32 " b38=%" PRId32 "\n", frame,
        simulation.timer_ticks, simulation.frame_ticks, o->x, o->y, o->floor_z,
        (unsigned)o->heading, game.camera.pitch,
        simulation.unk_144ae8, simulation.unk_144aec, simulation.unk_144af0,
        simulation.unk_144af4, simulation.unk_144af8, simulation.unk_144afc,
        simulation.unk_144b00, simulation.unk_144b1c, simulation.unk_144b24,
        simulation.unk_144b28, simulation.unk_144b2c, simulation.unk_144b34,
        simulation.unk_144b38);
    if (n < 0 || (size_t)n >= sizeof text) return cm_fail("state", frame, "state text overflow");
    {
        int add = snprintf(text+n, sizeof text-(size_t)n,
            "pc_a48=%" PRId32 " pc_ab0=%" PRId32 " pc_ab4=%" PRId32 " pc_ab8=%" PRId32
            " energy=%" PRId32 " pc_ax=%" PRId32 " pc_ay=%" PRId32
            " pc_hx=%" PRId32 " pc_hy=%" PRId32 " sword_frame=%" PRId32
            " p_3c=%" PRIu32 " p_40=%" PRIu32 " p_44=%" PRIu32 " p_48=%" PRIu32
            " auto_mode=%" PRId32 " auto_swing=%" PRId32 " auto_reset=%" PRId32
            " auto_pos=%" PRId32 " auto_time=%" PRIu32 " spell_timer=%" PRIu32 "\n",
            actors.pc_aim.weapon_aim_step, actors.pc.fire, actors.pc.cooldown, actors.pc.latch,
            events.energy, actors.pc_aim.aim_x, actors.pc_aim.aim_y,
            actors.pc.hand_x, actors.pc.hand_y, actors.pc.sword_frame,
            (uint32_t)pc->unk_003C, (uint32_t)pc->unk_0040,
            (uint32_t)pc->unk_0044, (uint32_t)pc->unk_0048,
            actors.pc.auto_s_mode, actors.pc.auto_swinging, actors.pc.auto_s_reset,
            actors.pc.reset_pos, actors.pc.auto_s_time, actors.pc.spell_timer);
        if (add < 0 || (size_t)add >= sizeof text-(size_t)n)
            return cm_fail("state", frame, "combat state text overflow");
        n += add;
    }
    for (i = 0; i < 4; ++i) {
        const ai_missile *m = &actors.missiles[i];
        const world_object *missile;
        int add;
        if (m->object < 0 || (unsigned)m->object >= game.object_count)
            return cm_fail("state/missile", i, "invalid player missile object");
        missile = &game.objects[m->object];
        add = snprintf(text+n, sizeof text-(size_t)n,
            "m%u_active=%" PRId32 " m%u_type=%" PRId32 " m%u_frame=%" PRId32
            " m%u_ticks=%" PRId32 " m%u_vertical=%" PRId32 " m%u_speed=%" PRId32
            " m%u_target=%" PRId32 " m%u_x=%" PRId32 " m%u_y=%" PRId32
            " m%u_z=%" PRId32 " m%u_heading=%u\n",
            i, m->active, i, m->type, i, m->frame, i, m->ticks, i, m->vertical,
            i, m->speed, i, m->target, i, missile->x, i, missile->y,
            i, missile->floor_z, i, (unsigned)missile->heading);
        if (add < 0 || (size_t)add >= sizeof text-(size_t)n)
            return cm_fail("state", frame, "missile state text overflow");
        n += add;
    }
    return platform_state_dump(directory, frame, text, (size_t)n);
}

int main(int argc, char **argv)
{
    const char *data = "orig/retail", *dump = NULL, *keys = NULL, *trace = NULL;
    const char *state_directory = NULL, *user = NULL;
    const char *control_mode = "classic";
    const uint8_t *pal_data;
    uint32_t pal_size;
    /* MenuOptions 04E830: initial aim (63,133), in desktop coordinates. */
    cm_input input = {.mouse_x = 446, .mouse_y = 134};
    cm_input physical = {.mouse_x = 446, .mouse_y = 134};
    cm_replay_frame recorded;
    unsigned frames = 0, frame = 0, bios_seed = 0;
    uint32_t audio_ticks = 0;
    int headless = 0, status = 1, playing = 0, divergence = 0, seed_explicit = 0, i;
    int modern = 0;
    int applied_settings[5], settings_applied = 0;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if ((!strcmp(argv[i], "--data") || !strcmp(argv[i], "--dump") ||
                  !strcmp(argv[i], "--keys") || !strcmp(argv[i], "--frames") ||
                  !strcmp(argv[i], "--state-dump") ||
                  !strcmp(argv[i], "--user") || !strcmp(argv[i], "--bios-seed") ||
                  !strcmp(argv[i], "--controls") ||
                  !strcmp(argv[i], "--replay")) && i + 1 < argc) {
            const char *arg = argv[i++];
            if (!strcmp(arg, "--data")) data = argv[i];
            else if (!strcmp(arg, "--dump")) dump = argv[i];
            else if (!strcmp(arg, "--keys")) keys = argv[i];
            else if (!strcmp(arg, "--state-dump")) state_directory = argv[i];
            else if (!strcmp(arg, "--user")) user = argv[i];
            else if (!strcmp(arg, "--controls")) control_mode = argv[i];
            else if (!strcmp(arg, "--bios-seed")) {
                if (number(argv[i], &bios_seed) || bios_seed > UINT16_MAX) {
                    cm_fail("arguments", 0, "--bios-seed must be 0..65535");
                    return 2;
                }
                seed_explicit = 1;
            }
            else if (!strcmp(arg, "--replay")) trace = argv[i];
            else if (number(argv[i], &frames) || !frames) {
                cm_fail("arguments", 0, "--frames must be 1..1000000");
                return 2;
            }
        } else {
            platform_report("usage: cybermage [--data DIR] [--user DIR] [--bios-seed N] [--controls classic|modern] [--headless] [--frames N] [--dump DIR] [--state-dump DIR] [--keys FILE | --replay TRACE]");
            return 2;
        }
    }
    if (strcmp(control_mode, "classic") && strcmp(control_mode, "modern")) {
        cm_fail("arguments", 0, "--controls must be classic or modern");
        return 2;
    }
    modern = !strcmp(control_mode, "modern");
    if (trace && modern) {
        cm_fail("arguments", 0, "original replays require classic controls");
        return 2;
    }
    if (trace && keys) { cm_fail("arguments", 0, "--keys and --replay are exclusive"); return 2; }
    if (headless && !frames && !trace) frames = 1;
    if (platform_init(headless)) goto out;
    if (!user) user = platform_user_dir();
    if (platform_directory(user)) goto out;
    controls_init(&controls);
    if (modern &&
        (make_path(controls_path, sizeof controls_path, user, "controls.cfg") ||
         controls_load(&controls, controls_path))) goto out;
    world_rng_init(&game.rng, (uint16_t)bios_seed);
    if (make_path(resource_path, sizeof resource_path, data, "HIPAK.RSC") ||
        make_path(strings_path, sizeof strings_path, data, "STRINGS.MSG") ||
        make_path(executable_path, sizeof executable_path, data, "CM_HI.EXE") ||
        platform_file_read(&executable, executable_path) ||
        cm_exe_init(&exe, &executable, executable_path) ||
        rsc_open(&hipak, resource_path) || platform_file_read(&strings, strings_path) ||
        rsc_entry(&hipak, 31, &pal_data, &pal_size) ||
        gfx_palette(palette, pal_data, pal_size, resource_path) ||
        ui_init(&ui, &hipak, &strings) || ui_load_hand_tables(&ui, data) ||
        snd_open(data) || game_names_read(user)) goto out;
    memcpy(base_palette, palette, sizeof base_palette);
    if (trace) snd_enable(0, 0); /* The original oracle runs its -x option. */
    if (keys && platform_keys(keys)) goto out;
    if (trace && replay_open(&replay, trace)) goto out;
    if (trace && replay.has_bios_seed && !seed_explicit)
        world_rng_init(&game.rng, replay.bios_seed);
    sim_init(&simulation, platform_ticks(0));
    if (ai_init_pc(&events, &simulation, &game.rng, &exe, 1, platform_ticks(0))) goto out;
    while (!input.quit && (!frames || frame < frames)) {
        uint32_t ticks = platform_ticks(frame);
        ui_action action;
        int shop_modal = 0;
        if (modern) {
            controls_context context = !playing || ui.screen != UI_GAME ||
                death_phase || events.selector_kind ? CONTROLS_MENU :
                ui.shop_kind || events.shop_requested.kind ? CONTROLS_SHOP :
                simulation.pc_mode == 7 ? CONTROLS_VEHICLE : CONTROLS_FOOT;
            if (platform_controls(1, context == CONTROLS_FOOT || context == CONTROLS_VEHICLE) ||
                platform_poll(&physical, frame)) goto out;
            controls_frame(&controls, context, &physical, &input);
        } else if (platform_poll(&input, frame)) goto out;
        if (snd_movie_active()) {
            int result = snd_movie_step(ticks, input.pressed[1] || input.pressed[0x1c],
                                       pixels, palette);
            if (result < 0) goto out;
            if (!result) {
                if (gfx_palette(palette, pal_data, pal_size, resource_path)) goto out;
                settings_applied = 0;
                ui_open(&ui, UI_TITLE);
            }
            goto audio_output;
        }
        if (trace) {
            int next = replay_next(&replay, &recorded);
            if (next < 0) goto out;
            if (!next) break;
            ticks = (uint32_t)recorded.values[CM_TRACE_TICKS];
            if (!playing) {
                if (new_game(data, (unsigned)recorded.values[CM_TRACE_SCENE],
                             (uint32_t)recorded.values[CM_TRACE_PREVIOUS])) goto out;
                audio_ticks = ticks;
                playing = 1;
            }
            if (!divergence) {
                int64_t expected = 0, actual = 0;
                const char *field = replay_compare(&game, &recorded, &expected, &actual);
                if (field) {
                    char text[STATE_TEXT_BYTES];
                    snprintf(text, sizeof text, "replay first divergence frame=%u field=%s expected=%" PRId64 " actual=%" PRId64,
                             frame, field, expected, actual);
                    platform_report(text);
                    divergence = 1;
                }
            }
            replay_input(&simulation, &recorded);
            input.raw_count = 0;
            memset(input.pressed, 0, sizeof input.pressed);
            memset(input.released, 0, sizeof input.released);
        }
        if (!trace && (!playing || ui.screen != UI_GAME)) {
            unsigned key;
            sim_input(&simulation, &input);
            if (!input.modern)
                for (key = 0; key < CM_KEY_COUNT; ++key)
                    if (!(simulation.kb_scan_status[key] & 10)) input.pressed[key] = 0;
        }
        ui.ticks = ticks;
        events.ticks = ticks;
        if (playing && !death_phase &&
            (game.persons[game.player_person].status == 2 || events.pc_dead_man)) {
            events.pc_dead_man = 0;
            if (death_start(ticks)) goto out;
        }
        if (!trace && playing && ui.screen == UI_GAME && !death_phase) {
            shop_modal = ui_shop_step(&ui, &events, &game, &input, ticks);
            if (shop_modal < 0) goto out;
            if (shop_modal) sim_input(&simulation, &input);
        }
        action = trace || shop_modal || death_phase == 1 || death_phase == 2 ?
            UI_NONE : ui_input(&ui, &input, ticks);
        if (playing && actors.difficulty != ui.options[8] &&
            ai_set_difficulty(&actors, &game, (unsigned)ui.options[8])) goto out;
        if (!settings_applied ||
            memcmp(applied_settings, ui.options + 3, sizeof applied_settings)) {
            if (ui_apply_settings(&ui, palette, base_palette)) goto out;
            memcpy(applied_settings, ui.options + 3, sizeof applied_settings);
            settings_applied = 1;
        }
        if (action == UI_QUIT) break;
        if (action == UI_CREDITS) {
            if (snd_movie_begin(data, SND_MOVIE_CREDITS, 0, ticks)) goto out;
            playing = 0;
            goto audio_output;
        }
        if (action == UI_SPEECH_REPLAY && snd_log_replay(&game, ui.slot)) goto out;
        if (action == UI_NEW_GAME || action == UI_TRAINING) {
            if (new_game(data, 1, ticks) || save_game(user, UI_SAVE_SLOTS)) goto out;
            input.raw_count = 0;
            playing = 1;
        }
        if (action == UI_SAVE_GAME || action == UI_LOAD_GAME) {
            if (action == UI_SAVE_GAME) {
                if (save_game(user, ui.slot)) goto out;
            } else {
                if (load_game(data, user, ui.slot, ticks)) goto out;
                playing = 1;
            }
            ui_open(&ui, UI_GAME);
        }
        if (playing) {
            ui_view view;
            if (death_phase == 1 || death_phase == 2) {
                sim_input(&simulation, &input);
                simulation.previous_ticks = simulation.bp_limit_time = ticks;
                ai_start_clock(&actors, ticks);
                if (ui_game_view(&ui, simulation.pc_mode, simulation.vehicle_type, &view) ||
                    render_set_view(view.x, view.y, view.width, view.height)) goto out;
                if (death_step(&input, ticks, trace != NULL)) goto out;
                goto audio_output;
            }
            if (ui.screen == UI_GAME && !shop_modal) {
                if (sim_frame(&simulation, &game, &input, ticks) ||
                    ai_pc_input(&actors, &simulation, &game) ||
                    events_step(&events, &simulation, &actors, &game) ||
                    ai_update_objects(&actors, &simulation, &game, ticks) ||
                    ai_pc_hand2hand(&actors, &simulation, &game) ||
                    events_countdown(&events, &simulation, &actors, &game)) goto out;
                if (events.scene_requested.pending && next_scene(data, user)) goto out;
                camera_follow();
                if (snd_tick(&game)) goto out;
                if (events.ending_requested) {
                    events.ending_requested = 0;
                    if (snd_movie_begin(data, SND_MOVIE_ENDING, 0, ticks)) goto out;
                    playing = 0;
                    goto audio_output;
                }
                if (events.recorder_requested) {
                    events.recorder_requested = 0;
                    ui_speech_recorder(&ui, events.speechLog);
                }
            } else {
                simulation.previous_ticks = simulation.bp_limit_time = ticks;
                ai_start_clock(&actors, ticks);
            }
            if (ui.screen == UI_GAME) {
                if (ui_game_view(&ui, simulation.pc_mode, simulation.vehicle_type, &view))
                    goto out;
            } else {
                view.x = view.y = 0;
                view.width = CM_WIDTH;
                view.height = CM_HEIGHT;
            }
            if (render_set_view(view.x, view.y, view.width, view.height)) goto out;
            if (ui.screen != UI_GAME) memset(pixels, 0, sizeof pixels);
            if (render_draw(&game, &game.camera, pixels)) goto out;
            if (ui.screen == UI_GAME && !shop_modal) {
                ui_hud hud = {0};
                if (ui_draw_backdrop(&ui, pixels, simulation.pc_mode,
                                     simulation.vehicle_type, view)) goto out;
                if (simulation.pc_mode == 7 &&
                    ui_draw_vehicle(&ui, &game, pixels, simulation.vehicle_type,
                                    simulation.flight_unk_144B48, simulation.unk_131418))
                    goto out;
                if ((simulation.pc_mode == 1 || simulation.pc_mode == 2 || simulation.pc_mode == 5) &&
                    ui_draw_live_hand(&ui, &game, pixels, simulation.pc_mode,
                                      &events, &actors, view)) goto out;
                if ((simulation.pc_mode == 3 || simulation.pc_mode == 4) &&
                    ui_draw_selector(&ui, &game, &events, pixels)) goto out;
                if (ui_hud_from_world(&hud, &game, &events, ui.hp_count, simulation.pc_mode) ||
                    ui_render_bells_and_whistles(&ui, pixels, &hud)) goto out;
                {
                    const snd_dialogue *dialogue = snd_text();
                    if (dialogue->active && !dialogue->spoken) {
                        int result = ui_text_message(&ui, &game, pixels, dialogue->lines,
                            dialogue->line_count, dialogue->revision, dialogue->object, view);
                        if (result < 0) goto out;
                        if (result > 0) snd_message_stop();
                    }
                }
            }
            if (modern && ui.screen == UI_GAME && !shop_modal &&
                controls_draw_map(&controls, &game, pixels)) goto out;
            if (ui_shop_draw(&ui, &events, &game, pixels)) goto out;
        }
audio_output:
        if (headless && !trace) {
            while (audio_ticks != ticks) {
                audio_mix(headless_pcm, 48000 / CM_TICK_HZ);
                ++audio_ticks;
            }
        } else audio_ticks = ticks;
        if (!snd_movie_active() && ui_draw(&ui, pixels)) goto out;
        if (!snd_movie_active() && playing && ui.screen != UI_GAME) {
            int aim_x = (input.mouse_x - 320) / 2;
            int aim_y = 200 - input.mouse_y / 2;
            if (ui_draw_menu_cursor(&ui, &game, pixels, aim_x, aim_y,
                                    ui_cursor_pose(aim_x, aim_y, input.mouse_buttons != 0))) goto out;
        }
        if (platform_present(pixels, palette) ||
            (dump && platform_dump(dump, frame, pixels, palette)) ||
            (playing && (state_directory || dump) &&
             state_dump(state_directory ? state_directory : dump, frame))) goto out;
        platform_wait(frame++);
    }
    if (trace) {
        char text[STATE_TEXT_BYTES];
        snprintf(text, sizeof text, "replay frames=%u player_state=%s", frame, divergence ? "DIVERGED" : "MATCH");
        platform_report(text);
    }
    status = divergence ? 3 : 0;
out:
    snd_movie_end();
    snd_close();
    replay_close(&replay);
    render_unload();
    world_close(&game);
    platform_file_release(&strings);
    platform_file_release(&executable);
    rsc_close(&hipak);
    platform_shutdown();
    return status;
}
