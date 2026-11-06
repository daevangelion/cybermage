#include "controls.h"
#include "events.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static sim_state s;
static events_state e;

static void arena(void)
{
    unsigned i;
    memset(&w, 0, sizeof w);
    sim_init(&s, 0);
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

int main(void)
{
    cm_input in = {0};
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
    arena();
    memset(&e, 0, sizeof e);
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
    assert(e.selector_kind == 1 && s.pc_mode == 3 && e.previous_mode == 1);
    e.spells[0] = 1;
    in.select_kind = 2;
    assert(!sim_modern_input(&s, &w, &in));
    assert(e.selector_kind == 2 && e.previous_mode == 1);
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
