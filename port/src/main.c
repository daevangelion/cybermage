#include "ai.h"
#include "audio.h"
#include "controls.h"
#include "events.h"
#include "gfx.h"
#include "platform.h"
#include "render.h"
#include "render_gl.h"
#include "replay.h"
#include "rsc.h"
#include "sim.h"
#include "target.h"
#include "tgv.h"
#include "ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_BYTES 1024
#define STATE_TEXT_BYTES (2048 + CM_OBJECT_MAX * 5)
#define ACTOR_STATE_LINE_BYTES 256

static uint8_t pixels[CM_WIDTH * CM_HEIGHT], palette[768];
static uint8_t base_palette[768];
static uint8_t save_data[UI_SAVE_MAX];
static uint8_t auto_data[UI_SAVE_MAX];
static uint8_t death_background[CM_WIDTH * CM_HEIGHT];
static unsigned death_columns[CM_WIDTH];
static uint32_t death_deadline, death_wait_time;
static int32_t death_frame_ticks;
static int death_phase;
static int gl_enabled, gl_recording, gl_overlay_valid;
static unsigned gl_width, gl_height;
static cm_overlay gl_overlay;
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
static char setup_path[PATH_BYTES];
static int auto_save = -1;

typedef struct {
    uint32_t frame;
    cm_replay *trace;
    cm_input *input;
} frame_draw;

static int draw_time(void *context, uint32_t site, uint32_t *ticks)
{
    frame_draw *draw = context;
    if (draw->trace) {
        if (replay_draw_clock(draw->trace, site, ticks)) return -1;
    } else *ticks = platform_ticks(draw->frame);
    ui.ticks = events.ticks = *ticks;
    return 0;
}

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

static int gl_overlay_begin(void)
{
    if (!gl_recording) {
        if (gfx_overlay_begin(&gl_overlay, pixels)) return -1;
        gl_recording = gl_overlay_valid = 1;
    }
    return 0;
}

static int gl_overlay_end(void)
{
    if (!gl_recording) return 0;
    gl_recording = 0;
    return gfx_overlay_end();
}

static int64_t floor_ratio(int64_t n, int64_t d)
{
    return n >= 0 ? n / d : -((-n + d - 1) / d);
}

static int canvas_edge(int64_t n, int64_t d, int limit, int upper)
{
    int64_t value = upper ? -floor_ratio(-n, d) : floor_ratio(n, d);
    return value < 0 ? 0 : value > limit ? limit : (int)value;
}

/* The HUD remains centred4:3. Extend the legacy view horizontally, preserving
 * its vertical focal length; do not stretch either the hands or HUD bitmaps. */
static int gl_world_view(ui_view view, ui_view *hole)
{
    int64_t num, den, margin_x, margin_y;
    int right, bottom;
    cm_gl_view native;
    if ((uint64_t)gl_width * CM_HEIGHT <= (uint64_t)gl_height * CM_WIDTH) {
        num = gl_width; den = CM_WIDTH;
    } else { num = gl_height; den = CM_HEIGHT; }
    margin_x = (int64_t)gl_width * den - CM_WIDTH * num;
    margin_y = (int64_t)gl_height * den - CM_HEIGHT * num;
    native.x = (int)(view.x * num / den);
    native.y = (int)((margin_y + 2 * view.y * num) / (2 * den));
    right = (int)gl_width - (int)((CM_WIDTH - view.x - view.width) * num / den);
    bottom = (int)((margin_y + 2 * (view.y + view.height) * num + 2 * den - 1) / (2 * den));
    native.width = (unsigned)(right - native.x);
    native.height = (unsigned)(bottom - native.y);
    native.reference_width = (unsigned)view.width;
    native.reference_height = (unsigned)view.height;
    native.preserve_outside = 0;
    hole->x = canvas_edge(2 * native.x * den - margin_x, 2 * num, CM_WIDTH, 0);
    hole->y = canvas_edge(2 * native.y * den - margin_y, 2 * num, CM_HEIGHT, 0);
    right = canvas_edge(2 * right * den - margin_x, 2 * num, CM_WIDTH, 1);
    bottom = canvas_edge(2 * bottom * den - margin_y, 2 * num, CM_HEIGHT, 1);
    hole->width = right - hole->x;
    hole->height = bottom - hole->y;
    return render_gl_draw_view(&game, &game.camera, &native);
}

/* Read the actual indexed GPU composite only at frozen-bitmap transitions,
 * never during ordinary gameplay or menu presentation. */
static int gl_bitmap_snapshot(void)
{
    if (gl_overlay_end() ||
        render_gl_read_canvas(gl_overlay_valid ? &gl_overlay : NULL, pixels) ||
        render_gl_clear(0) || gl_overlay_begin()) return -1;
    gfx_cover(pixels, sizeof pixels);
    return 0;
}

static void camera_follow(void)
{
    const world_object *o = &game.objects[game.player_object];
    /* Image1::Render 065D34: renderer performs CameraInit's 256-unit
     * quantization. Eye offset is view +4B170, not extra object translation. */
    game.camera.u = o->x;
    game.camera.v = o->y;
    game.camera.z = world_add32(world_add32(world_sub32(
        world_add32(o->floor_z, simulation.body_height), 0x1000),
        simulation.unk_144b38), actors.pc.hit_camera_z);
    game.camera.angle = o->heading;
}

/* Image1::Render -> primary Xox::RenderBellsAndWhistles. SaveGame0500B7
 * invokes this same pipeline before serializing, without advancing the world. */
static int game_draw(frame_draw *draw, unsigned phase)
{
    const cm_draw_clock clock = {draw, draw_time};
    ui_view view, backdrop_view;
    ui_hud hud = {0};
    int visual_mode = simulation.modern_aim && simulation.pc_mode == 3 ?
        simulation.previous_pc_mode : simulation.pc_mode;
    if (draw->trace) {
        int begin = replay_draw_begin(draw->trace, draw->frame, phase);
        if (begin <= 0) return begin;
    }
    if (ui_game_view(&ui, simulation.pc_mode, simulation.vehicle_type, &view) ||
        render_set_view(view.x, view.y, view.width, view.height)) return -1;
    {
        vis_view target = {view.x, view.y, view.width, view.height};
        if (target_query(&actors.targets, &game, &game.camera, &target)) return -1;
    }
    backdrop_view = view;
    if (gl_enabled) {
        /* A save redraw and the following game redraw are separate composites. */
        if (gl_overlay_end() || gl_world_view(view, &backdrop_view) || gl_overlay_begin())
            return -1;
    } else if (render_draw(&game, &game.camera, pixels)) return -1;
    if (ui_draw_backdrop(&ui, pixels, simulation.pc_mode,
                         simulation.vehicle_type, backdrop_view) ||
        ui_draw_pc_tint(&ui, pixels, &actors, view, 0)) return -1;
    if (simulation.pc_mode == 7 &&
        ((simulation.vehicle_type == 1 && ui_draw_vehicle_muzzle(&ui, &actors, pixels)) ||
         ui_draw_vehicle(&ui, &game, pixels, simulation.vehicle_type,
                          simulation.flight_unk_144B48, simulation.unk_131418)))
        return -1;
    if ((visual_mode == 1 || visual_mode == 2 || visual_mode == 5) &&
        ui_draw_live_hand(&ui, &game, pixels, visual_mode, &events, &actors, view))
        return -1;
    if (!simulation.modern_aim && (simulation.pc_mode == 3 || simulation.pc_mode == 4) &&
        events_selector_needs_input(&events) &&
        ui_draw_selector(&ui, &game, &events, pixels)) return -1;
    if (ui_draw_pc_tint(&ui, pixels, &actors, view, 1) ||
        ui_hud_from_world(&hud, &game, &events, ui.hp_count, visual_mode) ||
        ui_render_bells_and_whistles(&ui, pixels, &hud, &clock) ||
        ai_pc_draw_advance(&actors, &simulation, &game, &clock))
        return -1;
    {
        const snd_dialogue *dialogue = snd_text();
        if (dialogue->active && !dialogue->spoken) {
            int result = ui_text_message(&ui, &game, pixels, dialogue->lines,
                dialogue->line_count, dialogue->revision, dialogue->object, view);
            if (result < 0) return -1;
            if (result > 0) snd_message_stop();
        }
    }
    /* SpeechMessage019423/TextMessage01942A precedes DrawConsole01942F. */
    if (events_text_finish(&events, &game) ||
        events_countdown(&events, &simulation, &actors, &game)) return -1;
    /* Outer Aui::Animate057F38 polls before the NEXT HandleNextFrame.
     * SaveGame's nested Image1::Render does not run the outer poll loop. */
    if (draw->trace && phase == CM_DRAW_NORMAL && simulation.mouse_present) {
        if (!draw->input) return cm_fail("mouse replay", draw->frame, "missing physical input target");
        if (replay_mouse_poll(draw->trace, draw->frame, draw->input)) return -1;
    }
    return draw->trace ? replay_draw_end(draw->trace) : 0;
}

static void configure_input(void)
{
    simulation.input_device = ui.config.in_device;
    actors.pc.kb_fire = ui.config.key[0];
    actors.pc.kb_fire_bit = ui.config.mask[0];
#define KEY(field, index) \
    simulation.kb_##field = ui.config.key[index]; \
    simulation.kb_##field##_bit = ui.config.mask[index]
    KEY(hand, 1); KEY(exit, 2);
    KEY(side_toggle, 3); KEY(side_left, 4); KEY(side_right, 5);
    KEY(jump, 6); KEY(crouch, 7);
    KEY(look_down, 8); KEY(look_up, 9); KEY(look_center, 10);
    KEY(fly_up, 11); KEY(fly_down, 12); KEY(map, 13); KEY(vdu, 14);
#undef KEY
}

static int load_scene(const char *data, unsigned scene)
{
    uint32_t regen_timer = actors.pc.sp_regen_timer;
    int result;
    world_close(&game);
    render_unload();
    simulation.ai = &actors;
    simulation.events = &events;
    simulation.input_device=ui.config.in_device;
    if (world_load_scene(&game, data, scene)) return -1;
    result = ai_init(&actors, &game, (unsigned)ui.options[8]);
    actors.pc.sp_regen_timer = regen_timer;
    if (result || ai_set_difficulty(&actors, &game, (unsigned)ui.options[8])) return -1;
    ai_restore_pc_info(&events, &game);
    if (ai_load_tables(&actors, data) ||
        world_finalize_scene(&game) || sim_start_scene(&simulation, &game) ||
        events_init(&events, &game) || events_load_tables(&events, data) ||
        render_load(&game, &hipak) || snd_scene(&game)) return -1;
    if (gl_enabled && render_gl_load(&game, render_get_state(), palette)) return -1;
    actors.events = &events;
    configure_input();
    if (ai_pc_equip(&actors, &simulation, &game)) return -1;
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
    if (load_scene(data, scene)) return -1;
    actors.pc.cooldown = actors.pc.latch = 1; /* TitleMenu new game 055773. */
    return 0;
}

static int encode_save(uint32_t *size)
{
    unsigned difficulty = (unsigned)actors.difficulty;
    int result;
    if (ai_set_difficulty(&actors, &game, 1)) return -1;
    sim_save_export(&simulation, save.unk_6D4);
    save.unk_7B7[3] = (uint32_t)simulation.automap;
    save.FireCheck = (uint32_t)simulation.fire_check;
    save.FireDamage = (uint32_t)simulation.fire_damage;
    save.FireTilePanel = (uint32_t)simulation.fire_tile_panel;
    save.FireTileArt = (uint32_t)simulation.fire_tile_art;
    ai_save_export(&actors, &save);
    ui_text_export(&ui, &save);
    result = events_save_export(&events, &save) ||
        ui_save_game(&save, &game, save_data, sizeof save_data, size);
    if (ai_set_difficulty(&actors, &game, difficulty)) return -1;
    return result ? -1 : 0;
}

static int save_game(const char *user, unsigned slot, frame_draw *draw)
{
    char name[24];
    uint32_t size;
    if (slot > UI_SAVE_SLOTS) return cm_fail("SaveGame", slot, "invalid save slot");
    if (auto_save < -1 || auto_save >= EVENTS_AUTO_MAX)
        return cm_fail("GAMENAME.USR", slot, "invalid automatic save reference");
    camera_follow();
    if (game_draw(draw, CM_DRAW_SAVE)) return -1;
    snprintf(name, sizeof name, "SAVE%03u.DAT", slot);
    if (make_path(save_path, sizeof save_path, user, name) || encode_save(&size)) return -1;
    if (platform_file_write(save_path, save_data, size)) return -1;
    ui.restart_game = (int32_t)slot;
    if (slot == UI_SAVE_SLOTS) ui.restart_auto = auto_save;
    else ui.auto_ptr[slot] = (int16_t)auto_save;
    if (auto_save >= 0) ui.auto_slot[auto_save] = 1;
    memcpy(events.auto_ptr, ui.auto_ptr, sizeof events.auto_ptr);
    memcpy(events.auto_slot, ui.auto_slot, sizeof events.auto_slot);
    if(game_names_write()) return -1;
    events_reset_selectors(&events); /* SaveGame050A8F..050AC1, after file writes. */
    return 0;
}

static int save_sim_check(const uint8_t *data)
{
    sim_state checked;
    uint32_t values[29], fire[4];
    unsigned i;
    for (i = 0; i < 29; ++i) values[i] = cm_u32(data + 0x6d4 + 4 * i);
    for (i = 0; i < 4; ++i) fire[i] = cm_u32(data + 0x34f09 + 4 * i);
    return sim_save_import(&checked, values) || sim_save_fire_import(&checked, fire) ? -1 : 0;
}

static int import_sim(void)
{
    uint32_t fire[4] = {save.FireCheck, save.FireTilePanel, save.FireTileArt, save.FireDamage};
    simulation.automap = save.unk_7B7[3] & 1;
    return sim_save_import(&simulation, save.unk_6D4) ||
           sim_save_fire_import(&simulation, fire) ? -1 : 0;
}

static int load_game(const char *data, const char *user, unsigned slot, uint32_t ticks)
{
    uint8_t latch = simulation.kb_latch, modifiers = simulation.bios_shift;
    char name[24];
    cm_file file = {0};
    int result, saved_auto;
    int same_scene;
    unsigned scene;
    if (slot > UI_SAVE_SLOTS) return cm_fail("LoadGame", slot, "invalid save slot");
    saved_auto = slot == UI_SAVE_SLOTS ? ui.restart_auto : ui.auto_ptr[slot];
    if (saved_auto < -1 || saved_auto >= EVENTS_AUTO_MAX)
        return cm_fail("GAMENAME.USR", slot, "invalid automatic save reference");
    snprintf(name, sizeof name, "SAVE%03u.DAT", slot);
    if (make_path(save_path, sizeof save_path, user, name) ||
        platform_file_read(&file, save_path)) return -1;
    if (ui_save_scene(file.data, file.size, &scene) || save_sim_check(file.data)) {
        platform_file_release(&file);
        return -1;
    }
    auto_save = saved_auto;
    /* LoadGame050CC9..050D05 skips LoadScene when its resources are already
     * resident. Reconstructing that scene would consume its initialization RNG
     * again before overlaying the save, changing every later random decision. */
    same_scene = game.scenes.data && game.scene == scene;
    if (!same_scene) {
        sim_init(&simulation, ticks);
        simulation.kb_latch = latch;
        simulation.bios_shift = modifiers;
        result = load_scene(data, scene);
    } else {
        snd_kill(&game); /* Original KillSound050BBE precedes the overlay. */
        simulation.swim_loop = -1;
        actors.pc_sound = actors.pc.loop_sound = actors.pc.sword_hum_slot = -1;
        actors.pc_flash = 0;
        result = 0;
    }
    if (!result) result = ui_save_overlay(&save, &game, file.data, file.size);
    platform_file_release(&file);
    if (result || import_sim()) return -2; /* Scene replaced: caller must not resume it. */
    actors.difficulty = 1;
    events.ticks = ticks;
    if (ai_save_import(&actors, &game, &save) || events_save_import(&events, &save) ||
        events_text_restore(&events, &game) ||
        ui_text_restore(&ui, &save, events.text_serial))
        return -2;
    actors.events = &events;
    if (!same_scene) ai_start_clock(&actors, ticks);
    events.actors = &actors;
    memcpy(events.auto_ptr, ui.auto_ptr, sizeof events.auto_ptr);
    memcpy(events.auto_slot, ui.auto_slot, sizeof events.auto_slot);
    if (ai_set_difficulty(&actors, &game, (unsigned)ui.options[8])) return -2;
    ai_save_pc_info(&events, &game);
    if (ai_pc_equip(&actors, &simulation, &game)) return -2;
    ui.shop_kind = 0;
    death_phase = 0;
    camera_follow();
    ui.restart_game = (int32_t)slot;
    events_reset_selectors(&events); /* LoadGame0515F8..05162A, after import. */
    events.pc_dead_man = 0;
    simulation.modern_aim = 0;
    return 0;
}

static int load_requested(const char *data, const char *user, uint32_t ticks)
{
    ui_screen screen = ui.screen, return_screen = ui.return_screen;
    unsigned selected = ui.selected;
    int result = load_game(data, user, ui.slot, ticks);
    if (result) {
        if (result == -2) {
            snd_kill(&game);
            actors.pc.sword_hum_slot = -1;
            render_unload();
            world_close(&game);
            death_phase = 0;
            events.pc_dead_man = 0;
        }
        ui_open(&ui, result == -2 ? UI_LOAD : screen);
        ui.return_screen = result == -2 ? UI_TITLE : return_screen;
        ui.selected = selected;
        return result == -2 ? -1 : 0;
    }
    ui_open(&ui, UI_GAME);
    return 1;
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
        sim_auto_unpack(save_data, size, file.data, file.size) || save_sim_check(save_data) ||
        ui_save_overlay(&save, &game, save_data, size) ||
        events_save_import(&events, &save) ||
        events_text_restore(&events, &game) ||
        ui_text_restore(&ui, &save, events.text_serial)) goto out;
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
    if (import_sim()) goto out;
    if (auto_return(previous, scene)) goto out;
    result = 0;
out:
    platform_file_release(&file);
    return result;
}

static int next_scene(const char *data, const char *user, frame_draw *draw)
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
    return save_game(user, UI_SAVE_SLOTS, draw);
}

static int movie_start(const char *data, unsigned kind, uint32_t ticks)
{
    if (replay.file.data)
        return cm_fail(replay.path, replay.frame, "movies are not supported during original replay");
    if (snd_movie_begin(data, kind, 0, ticks)) return -1;
    actors.pc.sword_hum_slot = -1;
    return 0;
}

static int death_input(frame_draw *draw, uint32_t site, uint32_t *value)
{
    if (draw->trace) return replay_death_read(draw->trace, draw->frame, site, value);
    *value = site == 0x1555a ? simulation.kb_scan_status[1] : platform_ticks(draw->frame);
    return 0;
}

static int death_start(frame_draw *draw)
{
    unsigned x;
    uint32_t ticks;
    if (snd_kill_loops(&game, 1)) return -1;
    simulation.swim_loop = -1;
    actors.pc_sound = -1;
    actors.pc.loop_sound = -1;
    actors.pc.sword_hum_slot = -1;
    for (x = 0; x < CM_WIDTH; ++x) death_columns[x] = 7;
    if (death_input(draw, 0x153fd, &ticks)) return -1;
    death_deadline = ticks + 60;
    if (death_input(draw, 0x15403, &death_wait_time)) return -1;
    death_frame_ticks = simulation.frame_ticks;
    events.pc_dead_man = 1;
    death_phase = 1;
    return 0;
}

static ui_action menu_input(cm_input *input, uint32_t ticks)
{
    ui_action action;
    int menu_open=ui.screen!=UI_GAME;
    int options_open=ui.screen==UI_OPTIONS;
    if(input->quit) return UI_QUIT;
    /* Keyboard012F04/01340D owns Escape and all twenty shortcuts before
     * pause/save/load. Classic no-list hand mode also skips the foot menu. */
    if(ui.screen==UI_GAME && (events_selector_needs_input(&events) ||
       (!input->modern && simulation.pc_mode==3))) return UI_NONE;
    action = ui_input(&ui, input, ticks);
    if(options_open && ui.screen!=UI_OPTIONS) {
        /* OptOptions05422D writes CMDATA before applying difficulty. */
        ui_options_to_config(&ui);
        if(ui_config_save(&ui.config,setup_path)) return UI_ERROR;
        configure_input();
        if (game.scenes.data &&
            (ai_set_attack_difficulty(&actors, (unsigned)ui.options[8]) ||
             ai_set_difficulty(&actors, &game, (unsigned)ui.options[8])))
            return UI_ERROR;
    }
    if (ui.screen != UI_GAME && simulation.swim_loop != -1) {
        snd_abort(simulation.swim_loop, 4);
        simulation.swim_loop = -1;
    }
    if(menu_open && (ui.screen==UI_GAME || action==UI_LOAD_GAME ||
       action==UI_SAVE_GAME || action==UI_NEW_GAME || action==UI_TRAINING)) {
        /* MenuOptions return01BD28..01BD38 clears the keyboard. The modal
         * poll already delivered these scans; do not deliver them again to
         * HandleNextFrame, particularly after LoadGame replaces the PC. */
        memset(simulation.kb_scan_status,0,sizeof simulation.kb_scan_status);
        input->raw_count=0;
        memset(input->held,0,sizeof input->held);
        memset(input->pressed,0,sizeof input->pressed);
        memset(input->released,0,sizeof input->released);
    }
    return action;
}

static int death_backdrop(void)
{
    if (gl_enabled && (gl_overlay_end() || render_gl_clear(0) || gl_overlay_begin()))
        return -1;
    memcpy(pixels, death_background, sizeof pixels);
    gfx_cover(pixels, sizeof pixels);
    return 0;
}

static int death_step(frame_draw *draw, int no_sound)
{
    int song, finished;
    uint32_t escape;
    if (death_phase == 1) {
        const uint8_t *image;
        uint32_t size;
        unsigned x, y;
        /* PCDeath015410..015445 redraws the complete primary view/HUD, not
         * just the world. Its two initial time reads can straddle an IRQ. */
        if ((death_wait_time ^ UINT32_C(0x80000000)) <
            (death_deadline ^ UINT32_C(0x80000000))) {
            if (game_draw(draw, CM_DRAW_DEATH) ||
                death_input(draw, 0x1543f, &death_wait_time)) return -1;
            if ((death_wait_time ^ UINT32_C(0x80000000)) <
                (death_deadline ^ UINT32_C(0x80000000))) return 0;
        }
        if (simulation.pc_mode == 7 &&
            events_set_tank_mode(&events, &simulation, &game, 0)) return -1;
        if (rsc_entry(&hipak, 1, &image, &size)) return -1;
        if (size != 320u * 240u)
            return cm_fail(resource_path, 1, "invalid death backdrop dimensions");
        if (gl_enabled && gl_bitmap_snapshot()) return -1;
        for (y = 0; y < CM_HEIGHT; ++y)
            for (x = 0; x < CM_WIDTH; ++x)
                death_background[y * CM_WIDTH + x] = image[y / 2 * 320 + x / 2];
        death_phase = 2;
    }
    finished = ui_pc_death(pixels, death_background, death_columns, &game.rng);
    if (finished < 0) return -1;
    song = snd_check_song();
    if (song < 0) return -1;
    if (death_input(draw, 0x1555a, &escape)) return -1;
    if (escape) {
        simulation.kb_scan_status[1] = 0;
        snd_song_end();
    }
    if (song || escape || (no_sound && finished)) {
        /* The original virtual callback01559E is vPlaneErase015050, not
         * Image1::Render. Death menus get the backscreen, without HUD/RNG. */
        if (death_backdrop()) return -1;
        memset(simulation.kb_scan_status, 0, sizeof simulation.kb_scan_status);
        death_phase = 3;
        ui_open(&ui, UI_DEATH);
    }
    return 0;
}

static int state_dump(const char *directory, unsigned frame)
{
    static char text[STATE_TEXT_BYTES + CM_OBJECT_MAX * ACTOR_STATE_LINE_BYTES];
    const world_object *o;
    const world_person *pc;
    unsigned i;
    int n;
    if (game.person_count > CM_OBJECT_MAX || game.player_person >= game.person_count ||
        game.player_object >= game.object_count)
        return cm_fail("state", frame, "invalid actor/player pool");
    o = &game.objects[game.player_object];
    pc = &game.persons[game.player_person];
    n = snprintf(text, sizeof text,
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
            " auto_pos=%" PRId32 " auto_time=%" PRIu32 " spell_timer=%" PRIu32
            " glow_frame=%" PRId32 " glow_set=%" PRId32 " hand_type=%u"
            " muzzle_frame=%" PRId32 " muzzle_mode=%" PRId32 " muzzle_count=%" PRId32
            " selected_spell=%" PRId32 " selected_melee=%" PRId32 " selected_weapon=%" PRId32
            " pc_mode=%" PRId32 " render_x=%" PRId32 " render_y=%" PRId32 "\n",
            actors.pc_aim.weapon_aim_step, actors.pc.fire, actors.pc.cooldown, actors.pc.latch,
            events.energy, actors.pc_aim.aim_x, actors.pc_aim.aim_y,
            actors.pc.hand_x, actors.pc.hand_y, actors.pc.sword_frame,
            (uint32_t)pc->unk_003C, (uint32_t)pc->unk_0040,
            (uint32_t)pc->unk_0044, (uint32_t)pc->unk_0048,
            actors.pc.auto_s_mode, actors.pc.auto_swinging, actors.pc.auto_s_reset,
            actors.pc.reset_pos, actors.pc.auto_s_time, actors.pc.spell_timer,
            actors.pc.glow_frame, actors.pc.glow_set, actors.pc_aim.hand_type,
            actors.pc.muzzle_frame, actors.pc.muzzle_mode, actors.pc.muzzle_count,
            events.spell, events.hh_weapon, events.weapon, simulation.pc_mode,
            actors.pc_aim.render_x, actors.pc_aim.render_y);
        if (add < 0 || (size_t)add >= sizeof text-(size_t)n)
            return cm_fail("state", frame, "combat state text overflow");
        n += add;
    }
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        const ai_missile *m = &actors.missiles[i];
        const world_object *missile;
        int add;
        if (m->object < 0 || (unsigned)m->object >= game.object_count)
            return cm_fail("state/missile", i, "invalid missile object");
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
    {
        int add = snprintf(text + n, sizeof text - (size_t)n,
                           "swim=%" PRId32 " swim_deadline=%" PRIu32
                           " hp=%d rng0=%u rng1=%u rng2=%u bios_shift=%u\n"
                           "original_undefined_person=%u original_undefined_distance=%" PRId32 "\n"
                           "targets count=%u objects=",
                           simulation.unk_144b50, simulation.swim_damage_time,
                           (int)(int16_t)(uint16_t)pc->unk_0038,
                           (unsigned)game.rng.word[0], (unsigned)game.rng.word[1],
                           (unsigned)game.rng.word[2], (unsigned)simulation.bios_shift,
                           actors.undefined_distance_person, actors.undefined_distance_value,
                           actors.targets.count);
        if (actors.targets.count > CM_OBJECT_MAX || add < 0 ||
            (size_t)add >= sizeof text - (size_t)n)
            return cm_fail("state", frame, "target state text overflow");
        n += add;
        for (i = 0; i < actors.targets.count; ++i) {
            add = snprintf(text + n, sizeof text - (size_t)n, "%s%u",
                           i ? "," : "", (unsigned)actors.targets.objects[i]);
            if (add < 0 || (size_t)add >= sizeof text - (size_t)n)
                return cm_fail("state", frame, "target state text overflow");
            n += add;
        }
        if ((size_t)n + 1 >= sizeof text)
            return cm_fail("state", frame, "target state text overflow");
        text[n++] = '\n';
    }
    for (i = 0; i < game.person_count; ++i) {
        const world_person *p = &game.persons[i];
        const world_object *actor;
        int add;
        if (p->object < 0 || (unsigned)p->object >= game.object_count)
            return cm_fail("state/actor", i, "invalid actor object");
        actor = &game.objects[p->object];
        add = snprintf(text + n, sizeof text - (size_t)n,
            "actor=%u actor_object=%d actor_x=%" PRId32 " actor_y=%" PRId32
            " actor_z=%" PRId32 " actor_heading=%u actor_state=%d actor_status=%d"
            " actor_hp=%d actor_target=%" PRId32
            " actor_animation=%u actor_cursor=%u actor_active=%u\n",
            i, (int)p->object, actor->x, actor->y, actor->floor_z,
            (unsigned)actor->heading, (int)p->state, (int)p->status,
            (int)(int16_t)(uint16_t)p->unk_0038, p->target,
            (unsigned)p->unk_00BC, (unsigned)p->unk_00BD, (unsigned)actor->active);
        if (add < 0 || (size_t)add >= sizeof text - (size_t)n)
            return cm_fail("state", frame, "actor state text overflow");
        n += add;
    }
    return platform_state_dump(directory, frame, text, (size_t)n);
}

int main(int argc, char **argv)
{
    const char *data = "orig/retail", *dump = NULL, *keys = NULL, *trace = NULL;
    const char *state_directory = NULL, *user = NULL, *undefined_path = NULL;
    const char *control_mode = "classic";
    const char *renderer = "sw";
    const uint8_t *pal_data;
    uint32_t pal_size;
    /* MenuOptions 04E830: initial aim (63,133), in desktop coordinates. */
    cm_input input = {.mouse_x = 446, .mouse_y = 134};
    cm_input physical = {.mouse_x = 446, .mouse_y = 134};
    cm_replay_frame recorded;
    unsigned frames = 0, frame = 0, bios_seed = 0, replay_slot = 0;
    unsigned ticks_per_frame = 1;
    uint32_t audio_ticks = 0;
    int headless = 0, status = 1, playing = 0, divergence = 0, seed_explicit = 0, i;
    int modern = 0;
    int replay_load = 0;
    int applied_settings[5], settings_applied = 0;
    gl_enabled = gl_recording = gl_overlay_valid = 0;
    gl_width = gl_height = 0;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if ((!strcmp(argv[i], "--data") || !strcmp(argv[i], "--dump") ||
                  !strcmp(argv[i], "--keys") || !strcmp(argv[i], "--frames") ||
                  !strcmp(argv[i], "--state-dump") ||
                  !strcmp(argv[i], "--ticks-per-frame") ||
                  !strcmp(argv[i], "--user") || !strcmp(argv[i], "--bios-seed") ||
                  !strcmp(argv[i], "--controls") ||
                  !strcmp(argv[i], "--renderer") || !strcmp(argv[i], "--width") ||
                  !strcmp(argv[i], "--height") ||
                  !strcmp(argv[i], "--replay-load-slot") || !strcmp(argv[i], "--import-undefined") ||
                  !strcmp(argv[i], "--replay")) && i + 1 < argc) {
            const char *arg = argv[i++];
            if (!strcmp(arg, "--data")) data = argv[i];
            else if (!strcmp(arg, "--dump")) dump = argv[i];
            else if (!strcmp(arg, "--keys")) keys = argv[i];
            else if (!strcmp(arg, "--state-dump")) state_directory = argv[i];
            else if (!strcmp(arg, "--user")) user = argv[i];
            else if (!strcmp(arg, "--controls")) control_mode = argv[i];
            else if (!strcmp(arg, "--renderer")) renderer = argv[i];
            else if (!strcmp(arg, "--ticks-per-frame")) {
                if (number(argv[i], &ticks_per_frame) || !ticks_per_frame || ticks_per_frame > 8) {
                    cm_fail("arguments", 0, "--ticks-per-frame must be 1..8");
                    return 2;
                }
            }
            else if (!strcmp(arg, "--width") || !strcmp(arg, "--height")) {
                unsigned *dimension = !strcmp(arg, "--width") ? &gl_width : &gl_height;
                if (number(argv[i], dimension) || !*dimension || *dimension > CM_GL_SIZE_MAX) {
                    cm_fail("arguments", 0, "GL dimensions must be 1..8192");
                    return 2;
                }
            }
            else if (!strcmp(arg, "--import-undefined")) undefined_path = argv[i];
            else if (!strcmp(arg, "--replay-load-slot")) {
                if (number(argv[i], &replay_slot) || replay_slot > UI_SAVE_SLOTS) {
                    cm_fail("arguments", 0, "--replay-load-slot must be 0..10");
                    return 2;
                }
                replay_load = 1;
            }
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
            platform_report("usage: cybermage [--data DIR] [--user DIR] [--bios-seed N] [--controls classic|modern] [--renderer sw|gl] [--width N --height N] [--headless] [--ticks-per-frame N] [--frames N] [--dump DIR] [--state-dump DIR] [--keys FILE | --replay TRACE [--replay-load-slot N | --import-undefined PERSONS]]");
            return 2;
        }
    }
    if (strcmp(renderer, "sw") && strcmp(renderer, "gl")) {
        cm_fail("arguments", 0, "--renderer must be sw or gl");
        return 2;
    }
    gl_enabled = !strcmp(renderer, "gl");
    if (!gl_enabled && (gl_width || gl_height)) {
        cm_fail("arguments", 0, "--width and --height require --renderer gl");
        return 2;
    }
    if (!gl_width) gl_width = headless ? CM_WIDTH : 960;
    if (!gl_height) gl_height = headless ? CM_HEIGHT : 720;
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
    if (replay_load && (!trace || !user)) {
        cm_fail("arguments", 0, "--replay-load-slot requires --replay and an explicit --user directory");
        return 2;
    }
    if (undefined_path && (!trace || replay_load)) {
        cm_fail("arguments", 0, "--import-undefined is replay-only and cannot replace saved PERSON state");
        return 2;
    }
    if (headless && !frames && !trace) frames = 1;
    if (platform_init(headless)) goto out;
    if (headless && !trace && platform_tick_step(ticks_per_frame)) goto out;
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
        make_path(setup_path,sizeof setup_path,user,"CMDATA.CNF") ||
        ui_config_load(&ui.config,setup_path) ||
        snd_open(data) || game_names_read(user)) goto out;
    ui_options_from_config(&ui);
    memcpy(base_palette, palette, sizeof base_palette);
    if (gl_enabled) {
        if (render_gl_open(headless, gl_width, gl_height) || render_gl_palette(palette)) goto out;
        render_gl_dimensions(&gl_width, &gl_height);
        platform_report(render_gl_driver());
    }
    if (trace) snd_enable(0, 0); /* The original oracle runs its -x option. */
    if (keys && platform_keys(keys)) goto out;
    if (trace && replay_open(&replay, trace)) goto out;
    if (trace && replay.has_bios_seed && !seed_explicit)
        world_rng_init(&game.rng, replay.bios_seed);
    sim_init(&simulation, platform_ticks(0));
    simulation.input_device=ui.config.in_device;
    if (ai_init_pc(&events, &simulation, &game.rng, &exe, 1, platform_ticks(0))) goto out;
    configure_input();
    while (!input.quit && (!frames || frame < frames)) {
        uint32_t ticks = platform_ticks(frame);
        frame_draw draw = {frame, trace ? &replay : NULL, &input};
        ui_action action;
        int shop_modal = 0;
        int death_resume = 0;
        int movie_canvas = 0, drawable = 1;
        if (modern) {
            controls_context context = !playing || ui.screen != UI_GAME ||
                death_phase || events.selector_kind ? CONTROLS_MENU :
                ui.shop_kind || events.shop_requested.kind ? CONTROLS_SHOP :
                simulation.pc_mode == 7 ? CONTROLS_VEHICLE : CONTROLS_FOOT;
            if (platform_controls(1, context == CONTROLS_FOOT || context == CONTROLS_VEHICLE) ||
                platform_poll(&physical, frame)) goto out;
            controls_frame(&controls, context, &physical, &input);
        } else if (platform_poll(&input, frame)) goto out;
        if (gl_enabled) {
            unsigned width, height;
            if (input.render_reset && render_gl_reset()) goto out;
            platform_gl_size(&width, &height);
            drawable = width && height;
            if (drawable) {
                if (render_gl_resize(width, height)) goto out;
                gl_width = width; gl_height = height;
            }
        }
        if (snd_movie_active()) {
            int result;
            if (gl_enabled && (render_gl_clear(0) || gl_overlay_begin())) goto out;
            result = snd_movie_step(ticks, input.pressed[1] || input.pressed[0x1c],
                                   pixels, palette);
            if (result < 0) goto out;
            movie_canvas = 1;
            if (gl_enabled) gfx_cover(pixels, sizeof pixels);
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
                uint32_t start_ticks = (uint32_t)recorded.values[CM_TRACE_PREVIOUS];
                if (replay_load) {
                    if (load_game(data, user, replay_slot, start_ticks)) goto out;
                } else if (new_game(data, (unsigned)recorded.values[CM_TRACE_SCENE], start_ticks)) goto out;
                if (undefined_path) {
                    cm_file file = {0};
                    char message[PATH_BYTES + 128];
                    int result;
                    if (platform_file_read(&file, undefined_path)) goto out;
                    result = ai_import_person_undefined(&game, file.data, file.size);
                    platform_file_release(&file);
                    if (result) goto out;
                    snprintf(message, sizeof message,
                        "VERIFICATION ONLY: imported PERSON+0x24 for %u actors from %s; no other fields imported",
                        game.person_count, undefined_path);
                    platform_report(message);
                }
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
            /* No next original checkpoint exists for this unpaired terminal
             * entry. Do not invent modal inputs or execute an unseen death. */
            if (replay.next == replay.file.size) goto audio_output;
            replay_input(&simulation, &recorded);
            input.raw_count = 0;
            memset(input.pressed, 0, sizeof input.pressed);
            memset(input.released, 0, sizeof input.released);
            /* Virtual INT33 is available; actual CMDATA selection still gates
             * Sim's routing. No observed E3510/E3518 value is installed. */
            input.mouse_present = 1;
            input.joystick_present = 0;
            if (replay_mouse_input(&replay, frame, simulation.input_device == 1, &input))
                goto out;
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
        if (playing && !death_phase && events.pc_dead_man) {
            sim_clock(&simulation, ticks);
            if (simulation.replay_delta >= 0) simulation.frame_ticks = simulation.replay_delta;
            if (death_start(&draw)) goto out;
        }
        if (trace) {
            cm_replay_ui command;
            int next;
            /* PCDeath is modal INSIDE this HandleNextFrame. Complete its real
             * wait renders and melt RNG before accepting the menu selection. */
            while (death_phase == 1 || death_phase == 2)
                if (death_step(&draw, 1)) goto out;
            while ((next = replay_ui_next(&replay, frame, &command)) > 0) {
                ui.ticks = events.ticks = command.ticks;
                if (command.action == 1) {
                    if (death_phase) {
                        cm_fail(replay.ui_action_path, frame, "save is not a death menu action");
                        goto out;
                    }
                    if (command.slot < UI_SAVE_SLOTS)
                        memcpy(ui.names[command.slot], command.name, sizeof command.name);
                    if (save_game(user, command.slot, &draw)) goto out;
                } else {
                    unsigned slot = command.slot;
                    if (command.action == 3) {
                        /* Restart chooses the engine's real last save, not a
                         * checkpoint selected by expected trace output. */
                        if (ui.restart_game < 0 || ui.restart_game > UI_SAVE_SLOTS ||
                            slot != (unsigned)ui.restart_game) {
                            cm_fail(replay.ui_action_path, frame, "Restart save-slot divergence");
                            goto out;
                        }
                        slot = (unsigned)ui.restart_game;
                    }
                    if (load_game(data, user, slot, command.ticks)) goto out;
                    replay_input(&simulation, &recorded);
                }
                /* Both01BD28 and PCDeath015650 clear keyboard states before
                 * the remaining simulation, without importing game outputs. */
                memset(simulation.kb_scan_status, 0, sizeof simulation.kb_scan_status);
                ui_open(&ui, UI_GAME);
            }
            if (next < 0) goto out;
            if (death_phase == 3) {
                cm_fail(replay.ui_action_path, frame, "missing death Load/Restart input");
                goto out;
            }
        }
        if (!trace && playing && ui.screen == UI_GAME && !death_phase) {
            shop_modal = ui_shop_step(&ui, &events, &game, &input, ticks);
            if (shop_modal < 0) goto out;
            if (shop_modal) sim_input(&simulation, &input);
        }
        action = trace || shop_modal || death_phase == 1 || death_phase == 2 ?
            UI_NONE : menu_input(&input, ticks);
        if(action==UI_ERROR) goto out;
        if (!settings_applied ||
            memcmp(applied_settings, ui.options + 3, sizeof applied_settings)) {
            if (ui_apply_settings(&ui, palette, base_palette)) goto out;
            memcpy(applied_settings, ui.options + 3, sizeof applied_settings);
            settings_applied = 1;
        }
        if (action == UI_QUIT) break;
        if (action == UI_CREDITS) {
            if (movie_start(data, SND_MOVIE_CREDITS, ticks)) goto out;
            playing = 0;
            goto audio_output;
        }
        if (action == UI_SPEECH_REPLAY && snd_log_replay(&game, ui.slot)) goto out;
        if (action == UI_NEW_GAME || action == UI_TRAINING) {
            unsigned scene = action == UI_TRAINING ? 11u : 1u;
            if (new_game(data, scene, ticks) || save_game(user, UI_SAVE_SLOTS, &draw)) goto out;
            input.raw_count = 0;
            playing = 1;
        }
        if (action == UI_SAVE_GAME || action == UI_LOAD_GAME) {
            if (action == UI_SAVE_GAME) {
                if (save_game(user, ui.slot, &draw)) goto out;
                ui_open(&ui, UI_GAME);
            } else {
                int from_death = death_phase == 3;
                int loaded = load_requested(data, user, ticks);
                if (loaded > 0) {
                    playing = 1;
                    if (from_death) {
                        simulation.replay_delta = death_frame_ticks;
                        death_resume = 1;
                    }
                }
                else if (loaded < 0) {
                    if (gl_enabled && gl_bitmap_snapshot()) goto out;
                    playing = 0;
                }
            }
        }
        if (playing) {
            ui_view view, backdrop_view;
            if (death_phase == 1 || death_phase == 2) {
                sim_input(&simulation, &input);
                if (death_step(&draw, trace != NULL)) goto out;
                goto audio_output;
            }
            if (ui.screen == UI_GAME && !shop_modal) {
                uint32_t fast_ticks, normal_ticks;
                if (sim_frame(&simulation, &game, &input, ticks)) goto out;
                if (simulation.vdu_request) {
                    ui.vdu = (ui.vdu + 1) % 3;
                    simulation.vdu_request = 0;
                }
                if (death_resume) simulation.replay_delta = -1;
                if (ai_pc_input(&actors, &simulation, &game) ||
                    events_step(&events, &simulation, &actors, &game)) goto out;
                fast_ticks = trace ? recorded.fast_actor_ticks : platform_ticks(frame);
                normal_ticks = trace ? recorded.normal_actor_ticks : platform_ticks(frame);
                if (ai_update_objects(&actors, &simulation, &game, normal_ticks, fast_ticks) ||
                    ai_pc_hand2hand(&actors, &simulation, &game)) goto out;
                if (events.scene_requested.pending && next_scene(data, user, &draw)) goto out;
                camera_follow();
                if (snd_tick(&game)) goto out;
                if (events.ending_requested) {
                    events.ending_requested = 0;
                    if (movie_start(data, SND_MOVIE_ENDING, ticks)) goto out;
                    playing = 0;
                    goto audio_output;
                }
                if (events.recorder_requested) {
                    events.recorder_requested = 0;
                    ui_speech_recorder(&ui, events.speechLog);
                }
            } else if (!death_phase) {
                simulation.previous_ticks = simulation.bp_limit_time = ticks;
                ai_start_clock(&actors, ticks);
            }
            if (death_phase == 3) {
                if (death_backdrop()) goto out;
            } else if (ui.screen == UI_GAME && !shop_modal) {
                if (game_draw(&draw, CM_DRAW_NORMAL)) goto out;
            } else {
                if (ui.screen == UI_GAME) {
                    if (ui_game_view(&ui, simulation.pc_mode, simulation.vehicle_type, &view))
                        goto out;
                } else {
                    view.x = view.y = 0;
                    view.width = CM_WIDTH;
                    view.height = CM_HEIGHT;
                    memset(pixels, 0, sizeof pixels);
                }
                if (render_set_view(view.x, view.y, view.width, view.height)) goto out;
                backdrop_view = view;
                if (gl_enabled) {
                    if (gl_world_view(view, &backdrop_view) || gl_overlay_begin()) goto out;
                } else if (render_draw(&game, &game.camera, pixels)) goto out;
            }
            if (modern && ui.screen == UI_GAME && !shop_modal &&
                controls_draw_map(&controls, &game, pixels)) goto out;
            if (ui_shop_draw(&ui, &events, &game, pixels)) goto out;
        }
audio_output:
        /* Movie start keeps the last complete GL scene until its first decoded
         * bitmap. Death keeps its frozen HUD operators across melt frames. */
        if (gl_enabled && !gl_recording &&
            !(snd_movie_active() && !movie_canvas) && !death_phase) {
            if ((!playing && render_gl_clear(0)) || gl_overlay_begin()) goto out;
            if (!playing) gfx_cover(pixels, sizeof pixels);
        }
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
        if (gl_enabled) {
            if (gl_overlay_end()) goto out;
            if (drawable) {
                if (render_gl_present(gl_overlay_valid ? &gl_overlay : NULL) ||
                    (dump && platform_gl_dump(dump, frame, gl_width, gl_height,
                                               render_gl_read_rgba)) ||
                    platform_gl_swap()) goto out;
            } else if (dump) {
                cm_fail("GL dump", 0, "window has no drawable framebuffer");
                goto out;
            }
        } else if (platform_present(pixels, palette) ||
                   (dump && platform_dump(dump, frame, pixels, palette))) goto out;
        if (playing && (state_directory || dump) &&
            state_dump(state_directory ? state_directory : dump, frame)) goto out;
        platform_wait(frame++);
    }
    if (trace) {
        char text[STATE_TEXT_BYTES];
        snprintf(text, sizeof text, "replay frames=%u player_state=%s", frame, divergence ? "DIVERGED" : "MATCH");
        platform_report(text);
    }
    status = divergence ? 3 : 0;
out:
    if (gl_recording) (void)gl_overlay_end();
    snd_movie_end();
    snd_close();
    actors.pc.sword_hum_slot = -1;
    replay_close(&replay);
    render_gl_close();
    render_unload();
    world_close(&game);
    platform_file_release(&strings);
    platform_file_release(&executable);
    rsc_close(&hipak);
    platform_shutdown();
    return status;
}
