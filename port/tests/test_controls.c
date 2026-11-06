#include "controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void frame(cm_controls *c, controls_context context, cm_input *in, cm_input *out)
{
    controls_frame(c, context, in, out);
}

int main(void)
{
    cm_controls c;
    cm_input in = {0}, out;
    int yaw = 0, pitch = 0;
    unsigned i;
    const char config[] = "foot.forward = key:30\nfoot.jump = pad:2\nmouse_yaw = 0.25\nmouse_pitch = 0.25\n";
    controls_init(&c);
    in.held[0x11] = in.held[0x20] = 1;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(out.modern && out.move_speed == 640 && out.move_angle == 0xe000);
    assert(out.raw_count == 0 && !out.held[0x11]);
    memset(&in, 0, sizeof in);
    frame(&c, CONTROLS_FOOT, &in, &out);
    in.pad_axes[0] = 16384;
    in.pad_axes[1] = -16384;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(out.move_speed > 300 && out.move_speed < 500 && out.move_angle == 0xe000);
    memset(&in, 0, sizeof in);
    controls_init(&c);
    assert(!controls_parse(&c, (const uint8_t *)config, sizeof config - 1));
    in.mouse_dx = in.mouse_dy = 1;
    for (i = 0; i < 4; ++i) {
        frame(&c, CONTROLS_FOOT, &in, &out);
        yaw += out.look_yaw;
        pitch += out.look_pitch;
    }
    assert(yaw == -1 && pitch == 1);
    in.mouse_dx = in.mouse_dy = 0;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(!out.look_yaw && !out.look_pitch);
    controls_init(&c);
    in.mouse_buttons = 1;
    in.held[0x11] = 1;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(out.held[0x39] && out.move_speed);
    frame(&c, CONTROLS_MENU, &in, &out);
    assert(!out.mouse_buttons && !out.mouse_pressed && !out.held[0x48]);
    in.mouse_buttons = 0;
    in.held[0x11] = 0;
    frame(&c, CONTROLS_MENU, &in, &out);
    in.mouse_buttons = 1;
    frame(&c, CONTROLS_MENU, &in, &out);
    assert(out.mouse_buttons == 1 && out.mouse_pressed == 1);
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(!out.held[0x39]);
    controls_init(&c);
    memset(&in, 0, sizeof in);
    in.pad_buttons = 1u << 0;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(out.jump);
    in.pad_disconnected = 1;
    in.pad_buttons = 0;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(!out.jump);
    in.pad_disconnected = 0;
    in.held[0x11] = 1;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(out.move_speed);
    in.focus_lost = 1;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(!out.move_speed);
    in.focus_lost = 0;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(!out.move_speed);
    in.held[0x11] = 0;
    frame(&c, CONTROLS_FOOT, &in, &out);
    in.held[0x11] = 1;
    frame(&c, CONTROLS_FOOT, &in, &out);
    assert(out.move_speed);
    controls_init(&c);
    assert(controls_parse(&c, (const uint8_t *)"foot.jump = key:999\n", 20));
    assert(controls_parse(&c, (const uint8_t *)"unknown = 1\n", 12));
    puts("controls: normalisation, fractional look, context ownership, focus/disconnect and bindings PASS");
    return 0;
}
