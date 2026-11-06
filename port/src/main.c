#include "ai.h"
#include "audio.h"
#include "events.h"
#include "gfx.h"
#include "platform.h"
#include "render.h"
#include "replay.h"
#include "rsc.h"
#include "sim.h"
#include "ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_BYTES 1024
#define STATE_TEXT_BYTES 512

static uint8_t pixels[CM_WIDTH * CM_HEIGHT], palette[768];
static uint8_t save_data[UI_SAVE_MAX];
static world game;
static ai_state actors;
static events_state events;
static sim_state simulation;
static cm_ui ui;
static ui_save_state save;
static rsc hipak;
static cm_file strings;
static cm_replay replay;
static int16_t headless_pcm[48000 / CM_TICK_HZ * 2];
static char resource_path[PATH_BYTES], strings_path[PATH_BYTES], save_path[PATH_BYTES];
static char names_path[PATH_BYTES];

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

static int new_game(const char *data, unsigned scene, uint32_t ticks)
{
    uint8_t latch = simulation.kb_latch, modifiers = simulation.bios_shift;
    world_close(&game);
    render_unload();
    memset(&actors, 0, sizeof actors);
    memset(&events, 0, sizeof events);
    memset(&save, 0, sizeof save);
    sim_init(&simulation, ticks);
    simulation.kb_latch = latch;
    simulation.bios_shift = modifiers;
    simulation.ai = &actors;
    simulation.events = &events;
    if (world_load_scene(&game, data, scene) || ai_init(&actors, &game) ||
        ai_load_tables(&actors, data) ||
        world_finalize_scene(&game) || sim_start_scene(&simulation, &game) ||
        events_init(&events, &game) || events_load_tables(&events, data) ||
        render_load(&game, &hipak) || snd_scene(&game)) return -1;
    actors.events = &events;
    memcpy(events.auto_ptr, ui.auto_ptr, sizeof events.auto_ptr);
    memcpy(events.auto_slot, ui.auto_slot, sizeof events.auto_slot);
    ui_save_init(&save, &game);
    ui_open(&ui, UI_GAME);
    return 0;
}

static int state_dump(const char *directory, unsigned frame)
{
    char text[STATE_TEXT_BYTES];
    const world_object *o = &game.objects[game.player_object];
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
    return platform_state_dump(directory, frame, text, (size_t)n);
}

int main(int argc, char **argv)
{
    const char *data = "orig/retail", *dump = NULL, *keys = NULL, *trace = NULL;
    const char *state_directory = NULL, *user = NULL;
    const uint8_t *pal_data;
    uint32_t pal_size;
    cm_input input = {0};
    cm_replay_frame recorded;
    unsigned frames = 0, frame = 0, bios_seed = 0;
    uint32_t audio_ticks = 0;
    int headless = 0, status = 1, playing = 0, divergence = 0, seed_explicit = 0, i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if ((!strcmp(argv[i], "--data") || !strcmp(argv[i], "--dump") ||
                  !strcmp(argv[i], "--keys") || !strcmp(argv[i], "--frames") ||
                  !strcmp(argv[i], "--state-dump") ||
                  !strcmp(argv[i], "--user") || !strcmp(argv[i], "--bios-seed") ||
                  !strcmp(argv[i], "--replay")) && i + 1 < argc) {
            const char *arg = argv[i++];
            if (!strcmp(arg, "--data")) data = argv[i];
            else if (!strcmp(arg, "--dump")) dump = argv[i];
            else if (!strcmp(arg, "--keys")) keys = argv[i];
            else if (!strcmp(arg, "--state-dump")) state_directory = argv[i];
            else if (!strcmp(arg, "--user")) user = argv[i];
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
            platform_report("usage: cybermage [--data DIR] [--user DIR] [--bios-seed N] [--headless] [--frames N] [--dump DIR] [--state-dump DIR] [--keys FILE | --replay TRACE]");
            return 2;
        }
    }
    if (trace && keys) { cm_fail("arguments", 0, "--keys and --replay are exclusive"); return 2; }
    if (headless && !frames && !trace) frames = 1;
    if (platform_init(headless)) goto out;
    if (!user) user = platform_user_dir();
    if (platform_directory(user)) goto out;
    world_rng_init(&game.rng, (uint16_t)bios_seed);
    if (make_path(resource_path, sizeof resource_path, data, "HIPAK.RSC") ||
        make_path(strings_path, sizeof strings_path, data, "STRINGS.MSG") ||
        rsc_open(&hipak, resource_path) || platform_file_read(&strings, strings_path) ||
        rsc_entry(&hipak, 31, &pal_data, &pal_size) ||
        gfx_palette(palette, pal_data, pal_size, resource_path) ||
        ui_init(&ui, &hipak, &strings) || ui_load_hand_tables(&ui, data) ||
        snd_open(data) || game_names_read(user)) goto out;
    if (trace) snd_enable(0, 0); /* The original oracle runs its -x option. */
    if (keys && platform_keys(keys)) goto out;
    if (trace && replay_open(&replay, trace)) goto out;
    if (trace && replay.has_bios_seed && !seed_explicit)
        world_rng_init(&game.rng, replay.bios_seed);
    sim_init(&simulation, platform_ticks(0));
    while (!input.quit && (!frames || frame < frames)) {
        uint32_t ticks = platform_ticks(frame);
        ui_action action;
        if (platform_poll(&input, frame)) goto out;
        if (trace) {
            int next = replay_next(&replay, &recorded);
            if (next < 0) goto out;
            if (!next) break;
            ticks = (uint32_t)recorded.values[CM_TRACE_TICKS];
            if (!playing) {
                if (new_game(data, (unsigned)recorded.values[CM_TRACE_SCENE], ticks)) goto out;
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
            for (key = 0; key < CM_KEY_COUNT; ++key)
                if (!(simulation.kb_scan_status[key] & 10)) input.pressed[key] = 0;
        }
        ui.ticks = ticks;
        action = trace ? UI_NONE : ui_input(&ui, &input, ticks);
        if (action == UI_QUIT) break;
        if (action == UI_SPEECH_REPLAY && snd_log_replay(&game, ui.slot)) goto out;
        if (action == UI_NEW_GAME || action == UI_TRAINING) {
            if (new_game(data, 1, ticks)) goto out;
            input.raw_count = 0;
            playing = 1;
        }
        if (action == UI_SAVE_GAME || action == UI_LOAD_GAME) {
            char name[24];
            snprintf(name, sizeof name, "SAVE%03u.DAT", ui.slot);
            if (make_path(save_path, sizeof save_path, user, name)) goto out;
            if (action == UI_SAVE_GAME) {
                uint32_t size;
                sim_save_export(&simulation, save.unk_6D4);
                save.FireCheck = (uint32_t)simulation.fire_check;
                save.FireDamage = (uint32_t)simulation.fire_damage;
                save.FireTilePanel = (uint32_t)simulation.fire_tile_panel;
                save.FireTileArt = (uint32_t)simulation.fire_tile_art;
                ai_save_export(&actors, &save);
                if (events_save_export(&events, &save) ||
                    ui_save_game(&save, &game, save_data, sizeof save_data, &size) ||
                    platform_file_write(save_path, save_data, size)) goto out;
                ui.restart_game = (int32_t)ui.slot;
                memcpy(ui.auto_ptr, events.auto_ptr, sizeof ui.auto_ptr);
                memcpy(ui.auto_slot, events.auto_slot, sizeof ui.auto_slot);
                if (game_names_write()) goto out;
            } else {
                cm_file file = {0};
                if (platform_file_read(&file, save_path)) goto out;
                if (ui_load_game(&save, &game, file.data, file.size, data)) {
                    platform_file_release(&file);
                    goto out;
                }
                platform_file_release(&file);
                sim_init(&simulation, ticks);
                simulation.ai = &actors;
                simulation.events = &events;
                if (sim_save_import(&simulation, save.unk_6D4)) goto out;
                simulation.fire_check = (int32_t)save.FireCheck;
                simulation.fire_damage = (int32_t)save.FireDamage;
                simulation.fire_tile_panel = (int32_t)save.FireTilePanel;
                simulation.fire_tile_art = (int32_t)save.FireTileArt;
                memset(&actors, 0, sizeof actors);
                actors.pc_sound = actors.pc_learn_spell = -1;
                actors.difficulty = 1;
                if (ai_load_scene_definitions(&actors, &game) || ai_load_tables(&actors, data) ||
                    ai_save_import(&actors, &game, &save) ||
                    events_init(&events, &game) || events_load_tables(&events, data) ||
                    events_save_import(&events, &save) ||
                    render_load(&game, &hipak)) goto out;
                actors.events = &events;
                playing = 1;
            }
            ui_open(&ui, UI_GAME);
        }
        if (playing) {
            if (ui.screen == UI_GAME) {
                if (sim_frame(&simulation, &game, &input, ticks) ||
                    ai_pc_input(&actors, &simulation, &game) ||
                    events_step(&events, &simulation, &actors, &game) ||
                    ai_update_objects(&actors, &simulation, &game, ticks) ||
                    ai_pc_hand2hand(&actors, &simulation, &game)) goto out;
                camera_follow();
                if (snd_tick(&game)) goto out;
                if (events.recorder_requested) {
                    events.recorder_requested = 0;
                    ui_speech_recorder(&ui, events.speechLog);
                }
            } else {
                simulation.previous_ticks = simulation.bp_limit_time = ticks;
            }
            if (ui.screen != UI_GAME) memset(pixels, 0, sizeof pixels);
            if (render_draw(&game, &game.camera, pixels)) goto out;
            if (ui.screen == UI_GAME) {
                ui_hud hud = {0};
                ui_view view = {120, 90, 400, 300};
                if ((simulation.pc_mode == 1 || simulation.pc_mode == 2 || simulation.pc_mode == 5) &&
                    ui_draw_player_hand(&ui, &game, pixels, simulation.pc_mode,
                                         simulation.pc_mode == 2 ? events.hh_weapon :
                                         simulation.pc_mode == 5 ? events.spell : events.weapon,
                                         0, view)) goto out;
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
        }
        if (headless && !trace) {
            while (audio_ticks != ticks) {
                audio_mix(headless_pcm, 48000 / CM_TICK_HZ);
                ++audio_ticks;
            }
        } else audio_ticks = ticks;
        if (ui_draw(&ui, pixels)) goto out;
        if (playing && ui.screen != UI_GAME) {
            ui_view view = {0, 0, CM_WIDTH, CM_HEIGHT};
            if (ui_draw_player_hand(&ui, &game, pixels, -1, 0, 0, view)) goto out;
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
    snd_close();
    replay_close(&replay);
    render_unload();
    world_close(&game);
    platform_file_release(&strings);
    rsc_close(&hipak);
    platform_shutdown();
    return status;
}
