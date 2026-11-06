#include "ui.h"
#include "events.h"
#include "ai.h"
#include "render.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static cm_ui ui;
static world w;
static ui_save_state save;
static events_state events;
static ai_state actors;
static cm_overlay overlay;
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

static void vehicle_frame(rsc *hipak,const char *out,unsigned person,
                            int kind,unsigned frame)
{
    unsigned old_person=w.player_person,old_object=w.player_object;
    const world_object *o=&w.objects[w.persons[person].object];
    world_camera camera={0};
    ui_view view;
    w.player_person=person;w.player_object=(unsigned)w.persons[person].object;
    camera.u=o->x;camera.v=o->y;camera.z=o->floor_z+o->z+o->z/2;
    camera.angle=o->heading;
    assert(ui_game_view(&ui,7,kind,&view) == 0);
    assert(render_load(&w,hipak) == 0);
    assert(render_set_view(view.x,view.y,view.width,view.height) == 0);
    assert(render_draw(&w,&camera,screen) == 0);
    assert(ui_draw_backdrop(&ui,screen,7,kind,view) == 0);
    assert(ui_draw_vehicle(&ui,&w,screen,kind,0,0) == 0);
    assert(platform_dump(out,frame,screen,palette) == 0);
    render_unload();
    w.player_person=old_person;w.player_object=old_object;
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
    {
        uint8_t base[768], adjusted[768];
        ui_view view;
        memset(base,80,sizeof base);
        assert(ui_game_view(&ui,7,0,&view) == 0);
        assert(view.x == 80 && view.y == 32 && view.width == 480 && view.height == 256);
        assert(ui_game_view(&ui,7,1,&view) == 0);
        assert(view.x == 0 && view.y == 16 && view.width == 640 && view.height == 276);
        assert(ui_game_view(&ui,5,0,&view) == 0);
        assert(view.x == 120 && view.y == 90 && view.width == 400 && view.height == 300);
        ui.options[6] = 10; ui.options[7] = 5;
        assert(ui_apply_settings(&ui,adjusted,base) == 0 && adjusted[0] == 100);
        ui.options[6] = 32; ui.options[7] = 32;
        assert(ui_apply_settings(&ui,adjusted,base) == 0 && adjusted[0] == 252);
        ui.options[6] = -32; ui.options[7] = 32;
        assert(ui_apply_settings(&ui,adjusted,base) == 0 && adjusted[0] == 0);
        ui.options[6] = 33;
        assert(ui_apply_settings(&ui,adjusted,base) == -1);
        ui.options[6] = ui.options[7] = 0;
        assert(ui_game_view(&ui,7,2,&view) == -1);
    }
    {
        uint8_t monitor[3] = {0,255,1};
        uint8_t packed[16] = {8,0,0,0,12,0,0,0,2,0,0,255,2,0,1,2};
        ui_bitmap bitmap = {monitor,3,1};
        ui_hand hand = {0};
        memset(screen,9,sizeof screen);
        assert(gfx_overlay_begin(&overlay,screen) == 0);
        ui_draw_monitor(screen,0,0,&bitmap);
        assert(overlay.coverage[0] == 0 && screen[0] == 9);
        assert(overlay.coverage[1] == 1 && screen[1] == 255);
        hand.pixels.data = packed; hand.pixels.size = sizeof packed;
        hand.half_width = hand.half_height = 1;
        hand.view_width = CM_WIDTH; hand.view_height = CM_HEIGHT;
        hand.aim_y = 240;
        assert(ui_draw_xscale_aim(screen,&hand,NULL) == 0);
        assert(overlay.coverage[476*CM_WIDTH+320] == 1);
        assert(screen[476*CM_WIDTH+320] == 0);
        packed[0] = 15;
        assert(ui_draw_xscale_aim(screen,&hand,NULL) == -1);
        assert(gfx_overlay_end() == 0);
    }
    {
        unsigned columns[CM_WIDTH], i;
        world_rng rng = {0};
        for (i=0;i<CM_WIDTH;++i) columns[i] = CM_HEIGHT+7u;
        assert(ui_pc_death(screen,encoded,columns,&rng) == 1);
        columns[0] = 0;
        assert(ui_pc_death(screen,encoded,columns,&rng) == -1);
    }
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
    ui_open(&ui,UI_DEATH);
    assert(ui.selected == 3);
    assert(press(1) == UI_NONE && ui.screen == UI_DEATH);
    assert(press(72) == UI_NONE && ui.selected == 3);
    assert(press(28) == UI_LOAD_GAME);
    ui_open(&ui,UI_GAME);
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
    {
        ui_view view;
        int32_t x=ui.ACarX[0], y=ui.ACarY[0];
        int32_t hp=w.persons[w.player_person].unk_0038;
        assert(ui_game_view(&ui,7,0,&view) == 0);
        assert(ui_draw_backdrop(&ui,screen,7,0,view) == 0);
        assert(ui_draw_vehicle(&ui,&w,screen,0,0,0) == 0);
        assert(platform_dump(argv[1],7,screen,palette) == 0);
        assert(ui_game_view(&ui,7,1,&view) == 0);
        assert(ui_draw_backdrop(&ui,screen,7,1,view) == 0);
        assert(ui_draw_vehicle(&ui,&w,screen,1,10,6550) == 0);
        assert(platform_dump(argv[1],8,screen,palette) == 0);
        actors.pc.muzzle_mode=3;actors.pc.muzzle_count=0;
        assert(ui_draw_vehicle_muzzle(&ui,&actors,screen) == 0);
        actors.pc.muzzle_count=3;
        assert(ui_draw_vehicle_muzzle(&ui,&actors,screen) == -1);
        actors.pc.muzzle_mode=actors.pc.muzzle_count=0;
        ui.ACarX[0]=-1;
        assert(ui_draw_vehicle(&ui,&w,screen,1,0,0) == -1);
        ui.ACarX[0]=INT32_MIN;
        assert(ui_draw_vehicle(&ui,&w,screen,1,0,0) == -1);
        ui.ACarX[0]=CM_WIDTH;
        assert(ui_draw_vehicle(&ui,&w,screen,1,0,0) == -1);
        ui.ACarX[0]=x; ui.ACarY[0]=INT32_MAX;
        assert(ui_draw_vehicle(&ui,&w,screen,1,0,0) == -1);
        ui.ACarY[0]=y;
        {
            uint16_t width=ui.acar[0].width;
            const char *label=ui.strings[147];
            ui.acar[0].width=CM_WIDTH;
            assert(ui_draw_vehicle(&ui,&w,screen,1,0,0) == -1);
            ui.acar[0].width=width;
            ui.strings[147]="THIS SUPPLIED SPEED UNIT LABEL DOES NOT FIT IN THE DISPLAY BUFFER";
            assert(ui_draw_vehicle(&ui,&w,screen,1,0,0) == -1);
            ui.strings[147]=label;
        }
        w.persons[w.player_person].unk_0038=0;
        assert(ui_draw_vehicle(&ui,&w,screen,0,0,0) == -1);
        w.persons[w.player_person].unk_0038=hp;
        vehicle_frame(&hipak,argv[1],84,0,7);
    }
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
        uint8_t *record;
        memcpy(encoded,original.data,original.size);
        record=encoded+UI_SAVE_PREFIX;
        record[0]=record[1]=255;
        assert(ui_save_overlay(&save,&w,encoded,original.size) == -1);
        memcpy(encoded,original.data,original.size);
        record[2]=record[3]=255;
        assert(ui_save_overlay(&save,&w,encoded,original.size) == -1);
        memcpy(encoded,original.data,original.size);
        record[4]=record[5]=255;
        assert(ui_save_overlay(&save,&w,encoded,original.size) == -1);
        memcpy(encoded,original.data,original.size);
        encoded[0x6DC]=1;encoded[0x6DD]=encoded[0x6DE]=encoded[0x6DF]=0;
        assert(ui_save_overlay(&save,&w,encoded,original.size) == -1);
        assert(ui_save_overlay(&save,&w,original.data,original.size) == 0);
    }
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
        events.cash = 0;
        input.pressed[57] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,1) == 1 && events.cash == 0);
        events.cash = cash;
        {
            int cost = events.definitions[5].unk_0018;
            events.definitions[5].unk_0018 = -1;
            assert(ui_shop_step(&ui,&events,&w,&input,1) == -1 && events.cash == cash);
            events.definitions[5].unk_0018 = cost;
        }
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
        memset(&input,0,sizeof input); input.pressed[15] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,3) == 1 && ui.shop_selling);
        for (i=0;i<ui.shop_count;++i)
            if (ui.shop_slots[i] == slot) ui.shop_selected = i;
        memset(&input,0,sizeof input); input.pressed[15] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,4) == 1);
        assert(events.cash == cash+events.definitions[5].unk_0018/2);
        assert(events.items[slot].type == 0);
        memset(&input,0,sizeof input); input.pressed[1] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,5) == 1 && !ui.shop_kind);
        memset(&input,0,sizeof input);
        events.shop_requested.kind = 1;
        ui.shop_stock[0][0] = 0;
        assert(ui_shop_step(&ui,&events,&w,&input,6) == -1);
        ui.shop_kind = 0;
        ui.shop_stock[0][0] = 256;
        assert(ui_shop_step(&ui,&events,&w,&input,6) == -1);
        ui.shop_kind = 0; events.shop_requested.kind = 0;
        ui.shop_stock[0][0] = 5;
        events.shop_requested.kind = 3;
        events.shop_requested.screen = 1;
        events.shop_requested.arg = 1;
        memcpy(events.coderStr+8,"AB12",5);
        assert(ui_shop_step(&ui,&events,&w,&input,7) == 1);
        input.pressed[30] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,8) == 1);
        memset(&input,0,sizeof input); input.pressed[48] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,9) == 1);
        memset(&input,0,sizeof input); input.pressed[2] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,10) == 1);
        memset(&input,0,sizeof input); input.pressed[3] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,11) == 1);
        assert(strcmp(ui.shop_code,"AB12") == 0);
        events.events[1].state = 1; events.events[1].value[1] = 0;
        memset(&input,0,sizeof input); input.pressed[28] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,12) == 1 && ui.shop_success);
        assert(events.events[1].state == 0 && events.events[1].value[1] == 1 &&
               events.events[1].value[11] == -1);
        assert(ui_shop_draw(&ui,&events,&w,screen) == 0);
        assert(platform_dump(argv[1],6,screen,palette) == 0);
        memset(&input,0,sizeof input); input.pressed[1] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,13) == 1 && !ui.shop_kind);
        events.shop_requested.kind = 4;
        memset(&input,0,sizeof input);
        events.SlotVisitTime=10000;events.JackPot=500;
        assert(ui_shop_step(&ui,&events,&w,&input,14) == 1 && ui.shop_kind == 4);
        assert(events.JackPot==500);
        cash=events.cash;events.cash=INT32_MIN;ui.bet_amount=1;
        input.pressed[28]=1;
        assert(ui_shop_step(&ui,&events,&w,&input,25) == 1 && ui.bet_amount==1);
        memset(&input,0,sizeof input);input.pressed[2]=1;
        assert(ui_shop_step(&ui,&events,&w,&input,36) == 1 && ui.bet_amount==11);
        events.cash=cash;ui.bet_amount=0;
        memset(&input,0,sizeof input);ui.gambling_next=0;
        input.pressed[2] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,25) == 1);
        assert(ui.bet_amount == 1);
        memset(&input,0,sizeof input); input.pressed[28] = 1;
        cash=events.cash;
        assert(ui_shop_step(&ui,&events,&w,&input,36) == 1);
        assert(events.cash == cash-1 && ui.slot_spinning);
        memset(&input,0,sizeof input);
        for(i=0;i<110;++i) assert(ui_shop_step(&ui,&events,&w,&input,47+i) == 1);
        assert(!ui.slot_spinning);
        {
            static const unsigned combos[7][3]={{3,3,2},{3,3,3},{2,2,2},
                {0,0,2},{1,1,1},{0,0,0},{0,1,2}};
            static const int profit[7]={30,50,100,200,-1,500,0};
            unsigned test,reel,symbol;
            for(test=0;test<7;++test) {
                ui.bet_amount=10;ui.slot_spinning=1;events.JackPot=500;
                cash=events.cash;
                for(reel=0;reel<3;++reel) {
                    for(symbol=0;symbol<14;++symbol)
                        if(cm_u16(ui.slot_symbols+symbol*2)==combos[test][reel]) break;
                    assert(symbol<14);
                    ui.slot_reel[reel]=(symbol+13)%14;ui.slot_steps[reel]=1;
                }
                assert(ui_shop_step(&ui,&events,&w,&input,160+test) == 1);
                assert(!ui.slot_spinning);
                if(profit[test]>0) assert(events.cash==cash+10+profit[test]);
                else if(profit[test]<0)
                    assert(events.cash>=cash+220 && events.cash<=cash+510);
                else assert(events.cash==cash && events.JackPot==508);
                if(test==5) assert(events.JackPot==1727);
            }
        }
        assert(ui_shop_draw(&ui,&events,&w,screen) == 0);
        assert(platform_dump(argv[1],9,screen,palette) == 0);
        events.SlotVisitTime=-1000;events.JackPot=500;ui.ticks=2000;
        assert(ui_gambling_open(&ui,&events,&w) == 1);
        assert(events.JackPot>=515 && events.JackPot<=529);
        input.pressed[1] = 1;
        assert(ui_shop_step(&ui,&events,&w,&input,200) == 1 && !ui.shop_kind);
    }
    {
        const uint8_t *background;
        ui_view view = {120,90,400,300};
        ui_hud hud = {0};
        unsigned phase, i;
        assert(rsc_entry(&hipak, 1, &background, &size) == 0 && size == 320*240);
        background_view(background,NULL,view);
        assert(ui_draw_player_hand(&ui, &w, screen, 5, 0, 4, view) == 0);
        memcpy(encoded,screen,sizeof screen);
        actors.pc_aim.aim_x = ui.CenterAimX[1];
        actors.pc_aim.aim_y = ui.CenterAimY[1];
        actors.pc_aim.w_max_leftaimx = actors.pc_aim.w_max_botaimy = -1024;
        actors.pc_aim.w_max_rightaimx = actors.pc_aim.w_max_topaimy = 1024;
        actors.pc_aim.weapon_aim_step = 4;
        actors.pc.glow_set = ui.GlowSet[0];
        events.spell = 0;
        background_view(background,NULL,view);
        assert(ui_draw_live_hand(&ui,&w,screen,5,&events,&actors,view) == 0);
        assert(memcmp(encoded,screen,sizeof screen) == 0);
        actors.pc_aim.weapon_aim_step = 18;
        assert(ui_draw_live_hand(&ui,&w,screen,5,&events,&actors,view) == -1);
        actors.pc_aim.weapon_aim_step = 4;
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
        memset(screen,9,sizeof screen);
        assert(ui_render_bells_and_whistles(&ui,screen,&hud) == 0);
        memcpy(encoded,screen,sizeof screen);
        memset(screen,9,sizeof screen);ui.vdu=2;
        assert(ui_render_bells_and_whistles(&ui,screen,&hud) == 0);
        for(i=0;i<34;++i) {
            assert(memcmp(screen+(10+i)*CM_WIDTH+10,
                          encoded+(436+i)*CM_WIDTH+10,206) == 0);
            assert(memcmp(screen+(10+i)*CM_WIDTH+424,
                          encoded+(436+i)*CM_WIDTH+424,206) == 0);
        }
        ui.vdu=0;memcpy(encoded,screen,sizeof screen);
        assert(ui_render_bells_and_whistles(&ui,screen,&hud) == 0);
        assert(memcmp(encoded,screen,sizeof screen) == 0);
        ui.vdu=1;
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
            assert(ui_cursor_pose(63,133,0) == 2);
            assert(ui_cursor_pose(-73,20,1) == 15);
            assert(ui_draw_menu_cursor(&ui,&w,screen,63,133,
                                       ui_cursor_pose(63,133,0)) == 0);
            assert(platform_dump(argv[1],4,screen,palette) == 0);
            platform_file_release(&rendered);
        }
    }
    world_close(&w);
    {
        cm_input input={0};
        int cash;
        assert(world_load_scene(&w,"orig/retail",2) == 0);
        assert(ai_init(&actors,&w) == 0);
        assert(events_init(&events,&w) == 0);
        events.actors=&actors;events.cash=10000;events.PCBetOdds=3;
        events.shop_requested.kind=2;events.PCBet=0;events.PCBetPick=-1;
        assert(ui_shop_step(&ui,&events,&w,&input,201) == 1 && ui.shop_kind == 2);
        assert(ui_shop_draw(&ui,&events,&w,screen) == 0);
        assert(platform_dump(argv[1],10,screen,palette) == 0);
        input.pressed[2]=1;
        assert(ui_shop_step(&ui,&events,&w,&input,212) == 1 && ui.bet_amount == 1);
        memset(&input,0,sizeof input);input.pressed[48]=1;cash=events.cash;
        assert(ui_shop_step(&ui,&events,&w,&input,223) == 1);
        assert(events.PCBet==1 && events.PCBetPick==0 && events.cash==cash-1 && !ui.shop_kind);
        assert(w.persons[57].type==20 && w.persons[57].state==2 &&
               w.persons[57].unk_00BC==1 && w.persons[57].unk_001E==10);
        assert(w.objects[w.persons[57].object].x==0x1B2000);
        assert(events.events[28].value[3]==59 && events.events[243].value[1]==0);
        memset(&input,0,sizeof input);events.shop_requested.kind=2;
        assert(ui_shop_step(&ui,&events,&w,&input,245) == 0 && !ui.shop_kind);
        w.persons[58].status=w.persons[59].status=w.persons[60].status=2;
        events.shop_requested.kind=2;
        assert(ui_shop_step(&ui,&events,&w,&input,256) == 1);
        assert(events.cash==cash-1+3 && events.PCBet==0 && events.PCBetPick==-1);
        {
            unsigned pick;
            for(pick=0;pick<2;++pick) {
                events.PCBet=0;events.PCBetPick=-1;
                ui.shop_kind=2;ui.bet_amount=0;ui.gambling_next=0;
                memset(&input,0,sizeof input);input.mouse_pressed=1;
                input.mouse_x=ui.arena_x[3]*2+2;input.mouse_y=ui.arena_y[3]*2+2;
                assert(ui_shop_step(&ui,&events,&w,&input,300+pick*100) == 1 && ui.bet_amount==1);
                input.mouse_x=ui.arena_x[1]*2+2;input.mouse_y=ui.arena_y[1]*2+2;
                assert(ui_shop_step(&ui,&events,&w,&input,311+pick*100) == 1 && ui.bet_amount==0);
                input.mouse_x=ui.arena_x[3]*2+2;input.mouse_y=ui.arena_y[3]*2+2;
                assert(ui_shop_step(&ui,&events,&w,&input,322+pick*100) == 1 && ui.bet_amount==1);
                input.mouse_x=(pick?233:37)*2+2;input.mouse_y=159*2+2;
                assert(ui_shop_step(&ui,&events,&w,&input,333+pick*100) == 1 &&
                       !ui.shop_kind && events.PCBetPick==(int)pick);
            }
        }
        assert(events_arena_start(NULL,&w) == -1);
        assert(events_arena_start(&events,NULL) == -1);
        world_close(&w);
    }
    assert(world_load_scene(&w,"orig/retail",9) == 0);
    assert(ai_init(&actors,&w) == 0);
    assert(world_finalize_scene(&w) == 0);
    vehicle_frame(&hipak,argv[1],11,1,8);
    world_close(&w);
    printf("UI menus, settings, vehicle layouts, retail slots/arena, malformed inputs; GAMENAME288 and SAVE010 %u-byte exact roundtrips verified\n",original.size);
    platform_file_release(&original); platform_file_release(&names);
    platform_file_release(&strings); rsc_close(&hipak);
    platform_shutdown();
    return 0;
}
