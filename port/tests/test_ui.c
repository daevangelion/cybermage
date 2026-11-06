#include "ui.h"
#include "events.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static cm_ui ui;
static world w;
static ui_save_state save;
static events_state events;
static uint8_t encoded[UI_SAVE_MAX], screen[CM_WIDTH * CM_HEIGHT], palette[768];

static void background_view(const uint8_t *background, const cm_file *rendered,
                             ui_view view)
{
    unsigned x, y;
    for (y = 0; y < CM_HEIGHT; ++y)
        for (x = 0; x < CM_WIDTH; ++x)
            screen[y*CM_WIDTH+x] = background[(y/2)*320+x/2];
    if (rendered)
        for (y = (unsigned)view.y; y < (unsigned)(view.y+view.height); ++y)
            memcpy(screen+y*CM_WIDTH+view.x,rendered->data+y*CM_WIDTH+view.x,
                   (size_t)view.width);
}

static ui_action press(unsigned scan)
{
    cm_input in = {0};
    in.pressed[scan] = 1;
    return ui_input(&ui, &in, ui.ticks + 1);
}

int main(int argc, char **argv)
{
    rsc hipak = {0};
    cm_file strings = {0}, original = {0}, names = {0};
    const uint8_t *pal;
    uint32_t size, encoded_size;
    unsigned scene;
    uint8_t encoded_names[288];
    if (argc != 2 && argc != 3) return 2;
    assert(platform_init(1) == 0);
    assert(rsc_open(&hipak, "orig/retail/HIPAK.RSC") == 0);
    assert(platform_file_read(&strings, "orig/retail/STRINGS.MSG") == 0);
    assert(ui_init(&ui, &hipak, &strings) == 0);
    assert(ui_load_hand_tables(&ui, "orig/retail") == 0);
    assert(rsc_entry(&hipak, 31, &pal, &size) == 0);
    assert(gfx_palette(palette, pal, size, hipak.path) == 0);
    assert(ui_draw(&ui, screen) == 0);
    assert(platform_dump(argv[1], 0, screen, palette) == 0);
    assert(press(77) == UI_NONE && ui.selected == 1);
    assert(press(77) == UI_NONE && ui.selected == 2);
    assert(press(28) == UI_NONE && ui.screen == UI_OPTIONS);
    assert(ui_draw(&ui, screen) == 0);
    assert(platform_dump(argv[1], 1, screen, palette) == 0);
    assert(press(1) == UI_NONE && ui.screen == UI_TITLE);
    assert(press(28) == UI_NEW_GAME);
    ui_open(&ui, UI_GAME);
    assert(press(1) == UI_NONE && ui.screen == UI_PAUSE);
    assert(press(28) == UI_RESUME && ui.screen == UI_GAME);
    assert(press(63) == UI_NONE && ui.screen == UI_SAVE);
    assert(press(80) == UI_NONE && ui.selected == 1);
    assert(press(28) == UI_NONE && ui.naming);
    assert(press(28) == UI_SAVE_GAME && ui.slot == 1);
    assert(platform_file_read(&names, "orig/retail/GAMENAME.USR") == 0);
    assert(ui_game_names_read(&ui, names.data, names.size) == 0);
    assert(ui_game_names_write(&ui, encoded_names) == 0);
    assert(memcmp(names.data, encoded_names, names.size) == 0);
    assert(ui_game_names_read(&ui, names.data, names.size-1) == -1);
    assert(platform_file_read(&original, "orig/retail/SAVE010.DAT") == 0);
    assert(ui_save_scene(original.data, original.size, &scene) == 0 && scene == 1);
    assert(ui_save_scene(original.data, original.size-1, &scene) == -1);
    assert(ui_load_game(&save, &w, original.data, original.size, "orig/retail") == 0);
    assert(w.player_object == 936 && w.person_count == 152);
    assert(w.objects[936].x == (int32_t)cm_u32(original.data + 0x4C87 + 936 * 4));
    assert(w.objects[936].heading == cm_u16(original.data + 0x10941 + 936 * 2));
    assert(ui_save_game(&save, &w, encoded, sizeof encoded, &encoded_size) == 0);
    assert(encoded_size == original.size);
    if (memcmp(encoded, original.data, encoded_size)) {
        unsigned i;
        for (i = 0; i < encoded_size; ++i)
            if (encoded[i] != original.data[i]) {
                fprintf(stderr, "save differs at0x%X: %02X != %02X\n",i,encoded[i],original.data[i]);
                break;
            }
        return 1;
    }
    ++w.objects[w.player_object].x;
    assert(ui_save_game(&save, &w, encoded, sizeof encoded, &encoded_size) == 0);
    assert(cm_u32(encoded + 0x4C87 + 936 * 4) ==
           cm_u32(original.data + 0x4C87 + 936 * 4) + 1);
    assert(ui_save_game(&save, &w, encoded, UI_SAVE_PREFIX, &encoded_size) == -1);
    {
        cm_input input = {0};
        int cash, quantity, slot = -1;
        unsigned i;
        assert(events_init(&events,&w) == 0);
        events.cash = 10000;
        events.shop_requested.kind = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,0) == 1);
        assert(ui.shop_kind == 1 && events.shop_requested.kind == 0);
        assert(ui.shop_count == 16 && ui.shop_items[0] == 5);
        assert(ui_shop_draw(&ui,&events,&w,screen) == 0);
        assert(platform_dump(argv[1],5,screen,palette) == 0);
        ui.shop_selected = 0;
        cash = events.cash;
        input.pressed[28] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,1) == 1);
        assert(events.cash == cash-events.definitions[5].unk_0018);
        for (i=0;i<EVENTS_ITEM_MAX;++i)
            if (events.items[i].type == 5) slot = (int)i;
        assert(slot >= 0);
        quantity = events.items[slot].quantity;
        assert(quantity > 0);
        cash = events.cash;
        assert(ui_shop_step(&ui,&events,&w,&input,2) == 1);
        assert(events.cash == cash); /* Duplicate equipment is refused, no debit. */
        memset(&input,0,sizeof input); input.pressed[31] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,3) == 1 && ui.shop_selling);
        for (i=0;i<ui.shop_count;++i)
            if (ui.shop_slots[i] == slot) ui.shop_selected = i;
        memset(&input,0,sizeof input); input.pressed[28] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,4) == 1);
        assert(events.cash == cash+events.definitions[5].unk_0018/2);
        assert(events.items[slot].type == 0);
        memset(&input,0,sizeof input); input.pressed[1] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,5) == 1 && !ui.shop_kind);
    }
    {
        const uint8_t *background;
        ui_view view = {120,90,400,300};
        ui_hud hud = {0};
        unsigned phase, i;
        assert(rsc_entry(&hipak, 1, &background, &size) == 0 && size == 320*240);
        background_view(background,NULL,view);
        assert(ui_draw_player_hand(&ui, &w, screen, 5, 0, 4, view) == 0);
        hud.hp = cm_u16(original.data+UI_SAVE_PREFIX+0x38);
        hud.max_hp = cm_u16(original.data+UI_SAVE_PREFIX+0x3A);
        hud.energy = save.unk_130D50[864];
        hud.max_energy = save.unk_130D50[865];
        for (i = 0; i < 6; ++i) {
            hud.armor[i] = save.unk_130D50[(0x1313C8-0x130D50)/2+i];
            hud.max_armor[i] = save.unk_130D50[(0x1313D4-0x130D50)/2+i];
        }
        for (phase = 0; phase <= 2; ++phase) {
            hud.hp_count = phase;
            assert(ui_render_bells_and_whistles(&ui, screen, &hud) == 0);
        }
        assert(platform_dump(argv[1], 2, screen, palette) == 0);
        if (argc == 3) {
            cm_file rendered = {0};
            assert(platform_file_read(&rendered, argv[2]) == 0);
            assert(rendered.size == sizeof screen);
            background_view(background,&rendered,view);
            assert(ui_draw_player_hand(&ui,&w,screen,5,0,4,view) == 0);
            assert(ui_render_bells_and_whistles(&ui,screen,&hud) == 0);
            assert(platform_dump(argv[1],3,screen,palette) == 0);
            background_view(background,&rendered,view);
            ui_open(&ui,UI_PAUSE);
            assert(ui_draw(&ui,screen) == 0);
            view.x = view.y = 0; view.width = CM_WIDTH; view.height = CM_HEIGHT;
            assert(ui_cursor_pose(63,134,0) == 2);
            assert(ui_cursor_pose(-73,20,1) == 15);
            assert(ui_draw_menu_cursor(&ui,&w,screen,63,134,
                                       ui_cursor_pose(63,133,0)) == 0);
            assert(platform_dump(argv[1],4,screen,palette) == 0);
            platform_file_release(&rendered);
        }
    }
    printf("UI menus/transitions, malformed inputs, GAMENAME288-byte and SAVE010 %u-byte exact roundtrips; player936/persons152 verified\n",original.size);
    world_close(&w);
    platform_file_release(&original); platform_file_release(&names);
    platform_file_release(&strings); rsc_close(&hipak);
    platform_shutdown();
    return 0;
}
