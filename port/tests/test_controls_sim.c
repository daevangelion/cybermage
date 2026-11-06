#include "controls.h"
#include "events.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static sim_state s;
static events_state e;

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
    assert(s.movement_request == 640 && s.movement_angle == 0xe000);
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
    puts("controls sim adapter: movement, look bounds and distinct aim/jump PASS");
    return 0;
}
