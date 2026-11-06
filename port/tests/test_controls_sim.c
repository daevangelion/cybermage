#include "controls.h"
#include "events.h"
#include "ai.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static sim_state s;
static events_state e;
static ai_state a;
static const char *data_dir;

static void arena(void)
{
    unsigned i;
    memset(&w, 0, sizeof w);
    memset(&a, 0, sizeof a);
    assert(!ai_load_tables(&a, data_dir));
    sim_init(&s, 0);
    s.ai = &a;
    a.events = &e;
    s.pc_mode = 5;
    w.top_level = 3;
    w.object_count = 2;
    w.person_count = 1;
    w.player_object = 1;
    w.player_person = 0;
    w.objects[1].person = 0;
    w.persons[0].object = 1;
    w.objects[1].active = 1;
    w.objects[1].x = w.objects[1].y = 16 * 65536 + 32768;
    w.objects[1].z = 0x3800;
    w.objects[1].radius = w.objects[1].height = 0x1000;
    for (i = 0; i < CM_MAP_CELLS; ++i) w.cells[0][i].floor = 1;
    world_trig_init(&w.trig);
    assert(!world_make_omap(&w));
    assert(!sim_start_scene(&s, &w));
}

static void mouse_arena(void)
{
    arena();
    s.input_device = 1; /* Selected configuration, independent of driver presence. */
}

static void classic_mouse(void)
{
    static const int16_t motion[] = {-32768, -33, -32, 0, 32, 33, 32767};
    static const int32_t yaw[] = {128, 66, 32, 0, -32, -66, -128};
    cm_input in = {0};
    unsigned i;
    mouse_arena();
    in.mouse_present = 1;
    in.mouse_buttons = 4; /* SDL right, not DOS button bit 2. */
    a.pc.cursor_flags = 3;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(a.pc.cursor_flags == 2 && s.running);
    mouse_arena();
    in.mouse_buttons = 2; /* Middle is not right. */
    a.pc.cursor_flags = 3;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(!a.pc.cursor_flags && !s.movement_request);
    mouse_arena();
    in.mouse_buttons = 0;
    in.mouse_press_count[1] = 1;
    a.pc.cursor_flags = 3;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(a.pc.cursor_flags == 2 && s.movement_request > 0);
    mouse_arena();
    in.mouse_press_count[1] = 0;
    in.mouse_buttons = 1;
    a.pc.cursor_flags = 2;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(!s.primary_fire && !a.pc.cursor_flags);
    assert(!sim_frame(&s, &w, &in, 4));
    assert(s.primary_fire);
    mouse_arena();
    in.mouse_buttons = 0;
    in.mouse_press_count[0] = 1; /* A complete click between polls still fires. */
    assert(!sim_frame(&s, &w, &in, 2));
    assert(s.primary_fire);
    in.mouse_press_count[0] = 0;
    for (i = 0; i < sizeof motion / sizeof *motion; ++i) {
        mouse_arena();
        in.mouse_mickey_x = motion[i];
        in.mouse_mickey_y = -100;
        w.camera.pitch = 96;
        assert(!sim_frame(&s, &w, &in, 2));
        assert(s.yaw_request == yaw[i] && s.unk_144af0 == yaw[i]);
        assert(s.unk_144aec == 48 && !s.movement_request && !s.unk_144ae8);
        assert(w.camera.pitch == 96); /* DOS Y motion walks; it is not modern look. */
    }
    mouse_arena();
    in.mouse_mickey_x = 100;
    in.mouse_mickey_y = -100;
    s.kb_scan_status[0x48] = s.kb_scan_status[0x4d] = 12;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(s.yaw_request == -16 && s.unk_144af0 == -16);
    assert(s.movement_request == 16 && s.unk_144aec == 8);
    mouse_arena();
    in.mouse_mickey_x = in.mouse_mickey_y = 0;
    in.mouse_buttons = 4;
    s.kb_scan_status[0x4f] = 1;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(s.movement_angle == 0x2000); /* RMB turns side-left into forward-left. */
    mouse_arena();
    s.kb_scan_status[0x51] = 1;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(s.movement_angle == 0xe000);
    mouse_arena();
    s.bios_shift = 2;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(!s.running && s.movement_request > 0);
    mouse_arena();
    in.mouse_present = 0;
    in.mouse_mickey_x = in.mouse_mickey_y = 100;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(!s.movement_request && !s.yaw_request && !s.unk_144aec);
    for (i = 0; i < 3; i += 2) {
        arena();
        s.input_device = (int32_t)i;
        in.mouse_present = 1;
        assert(!sim_frame(&s, &w, &in, 2));
        assert(!s.mouse_present && !s.movement_request && !s.yaw_request && !s.unk_144aec);
    }
}

static int32_t speed(int magnitude, int run)
{
    cm_input input = {0};
    unsigned i;
    int32_t before;
    arena();
    input.modern = 1;
    input.move_speed = magnitude;
    input.run = (uint8_t)run;
    for (i = 1; i <= 40; ++i) assert(!sim_frame(&s, &w, &input, i * 2));
    before = w.objects[1].x;
    assert(!sim_frame(&s, &w, &input, 82));
    assert(s.unk_144aec > 0);
    return w.objects[1].x - before;
}

int main(int argc, char **argv)
{
    cm_input in = {0};
    assert(argc == 2);
    data_dir = argv[1];
    classic_mouse();
    arena();
    w.object_count = w.person_count = 1;
    w.player_object = w.player_person = 0;
    s.pc_mode = 5;
    s.events = &e;
    in.modern = 1;
    in.move_speed = 640;
    in.move_angle = 0xe000;
    in.look_yaw = -5;
    in.look_pitch = 300;
    in.jump = 1;
    assert(sim_modern_input(&s, &w, &in) == 1);
    assert(s.movement_request == 48 && s.movement_angle == 0xe000);
    assert(w.objects[0].heading == 65531 && w.camera.pitch == 224);
    assert(s.pc_mode == 5);
    in.jump = 0;
    in.aim = 1;
    assert(!sim_modern_input(&s, &w, &in));
    assert(s.modern_aim && s.pc_mode == 3 && s.previous_pc_mode == 5);
    assert(!s.movement_request);
    in.aim = 0;
    in.look_pitch = -1000;
    assert(!sim_modern_input(&s, &w, &in));
    assert(!s.modern_aim && s.pc_mode == 5 && w.camera.pitch == -224);
    {
        int32_t walk = speed(640, 0);
        int32_t half = speed(320, 0);
        int32_t run = speed(768, 1);
        assert(walk > 0 && walk == half * 2 && run == walk * 2);
    }
    arena();
    memset(&in, 0, sizeof in);
    in.modern = 1;
    in.jump = 1;
    assert(!sim_frame(&s, &w, &in, 2));
    assert(s.unk_144b1c > 0 && w.objects[1].floor_z > 0 && s.pc_mode == 5);
    /* Dedicated jump does not open the stationary Ctrl hand screen. */
    arena();memset(&e,0,sizeof e);s.events=&e;e.actors=&a;e.ambient_slot=-1;
    w.persons[0].unk_0038=40<<16|17;s.unk_144b30=1;
    sim_scan(&s,0x9C); /* Genuine post-load Enter release primes IRQ9's latch. */
    memset(&in,0,sizeof in);in.raw_count=1;in.raw_scans[0]=0x1D;
    assert(!sim_frame(&s,&w,&in,2));
    assert(s.pc_mode==3 && s.previous_pc_mode==5 && !s.unk_144b1c);
    assert(a.pc.latch==1 && !a.pc.fire && !a.pc.cooldown);
    assert(a.pc_aim.aim_x==a.pc_rules.center_aim_x[0] && a.pc_aim.aim_y==a.pc_rules.center_aim_y[0]);
    in.raw_count=2;in.raw_scans[0]=0xE0;in.raw_scans[1]=0x48;
    assert(!sim_frame(&s,&w,&in,4) && s.pc_mode==3 && !s.movement_request && !s.yaw_request);
    in.raw_count=1;in.raw_scans[0]=0x9D;
    assert(!sim_frame(&s,&w,&in,6) && s.pc_mode==5 && a.pc.latch==1);
    assert(a.pc_aim.aim_x==a.pc_rules.center_aim_x[1] && a.pc_aim.aim_y==a.pc_rules.center_aim_y[1]);
    e.items[0].type=120;e.items[0].kind=0;e.items[1].type=6;e.items[1].kind=9;e.items[1].quantity=1;
    in.raw_scans[0]=0x3D;
    assert(!sim_frame(&s,&w,&in,8) && s.pc_mode==3 && e.selector_request==3);
    assert(!events_selector_needs_input(&e) && !events_input(&e,&s,&a,&w));
    assert(e.selector_kind==3 && e.selectors[2].count==2 && s.previous_pc_mode==5);
    in.raw_count=0;
    assert(!sim_frame(&s,&w,&in,10) && s.pc_mode==3 && !s.movement_request);
    assert(events_selector_needs_input(&e) && !events_input(&e,&s,&a,&w));
    in.raw_count=1;in.raw_scans[0]=0x3E;
    assert(!sim_frame(&s,&w,&in,12) && e.selector_confirm && s.pc_mode==3);
    assert(!ai_pc_input(&a,&s,&w) && !events_input(&e,&s,&a,&w));
    assert(s.pc_mode==5 && !e.selector_kind && !e.items[1].quantity && !e.items[1].type);
    assert((uint16_t)w.persons[0].unk_0038==40);
    arena();
    memset(&e, 0, sizeof e);
    e.actors = &a;
    memset(&in, 0, sizeof in);
    s.events = &e;
    s.pc_mode = 1;
    e.items[0].type = 4;
    e.items[0].kind = 6;
    e.items[1].type = 31;
    e.items[1].kind = 6;
    e.items[1].subtype = 1;
    in.modern = 1;
    in.select_kind = 1;
    in.cycle = 1;
    assert(!sim_modern_input(&s, &w, &in));
    assert(e.weapon == 1 && s.pc_mode == 1 && !e.selector_kind);
    in.cycle = 0;
    in.select_open = 1;
    assert(!sim_modern_input(&s, &w, &in));
    assert(e.selector_kind == 1 && s.pc_mode == 3 && s.previous_pc_mode == 1);
    e.spells[0] = 1;
    in.select_kind = 2;
    assert(!sim_modern_input(&s, &w, &in));
    assert(e.selector_kind == 2 && s.previous_pc_mode == 1);
    in.select_open = 0;
    in.select_slot = 1;
    assert(!sim_modern_input(&s, &w, &in));
    assert(e.spell == 0 && s.pc_mode == 5 && !e.selector_kind);
    e.items[2].type = 25;
    e.items[2].kind = 0;
    in.select_slot = 0;
    in.select_kind = 3;
    in.cycle = 1;
    assert(!sim_modern_input(&s, &w, &in));
    assert(e.selector_kind == 3 && s.pc_mode == 3 && !s.flight_mode);
    in.cycle = 0;
    in.select_slot = 2;
    assert(!sim_modern_input(&s, &w, &in));
    assert(!e.selector_kind && s.flight_mode == 1);
    in.select_slot = 0;
    in.pressed[0x21] = 1;
    assert(!sim_modern_input(&s, &w, &in));
    assert(!s.flight_mode);
    puts("controls sim adapter: movement, look bounds and distinct aim/jump PASS");
    return 0;
}
