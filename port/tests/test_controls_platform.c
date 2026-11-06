#include "controls.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void send_key(Uint32 type, SDL_Scancode scan)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.key.keysym.scancode = scan;
    assert(SDL_PushEvent(&e) == 1);
}

int main(void)
{
    cm_controls c;
    cm_input physical = {0}, out;
    SDL_Event e;
    int index;
    SDL_JoystickID id;
    assert(!platform_init(1));
    assert(!platform_controls(1, 1));
    controls_init(&c);
    index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 21, 0);
    assert(index >= 0);
    id = SDL_JoystickGetDeviceInstanceID(index);
    assert(id >= 0);
    assert(!platform_poll(&physical, 0));
    send_key(SDL_KEYDOWN, SDL_SCANCODE_W);
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEMOTION;
    e.motion.xrel = 3;
    e.motion.yrel = 1;
    assert(SDL_PushEvent(&e) == 1);
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.which = id;
    e.caxis.axis = SDL_CONTROLLER_AXIS_LEFTX;
    e.caxis.value = 16384;
    assert(SDL_PushEvent(&e) == 1);
    e.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    e.caxis.value = 20000;
    assert(SDL_PushEvent(&e) == 1);
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.which = id;
    e.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    assert(SDL_PushEvent(&e) == 1);
    assert(!platform_poll(&physical, 1));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(out.move_speed == 640 && out.move_angle > 0xc000);
    assert(out.look_yaw == -30 && out.jump && out.held[0x39]);
    assert(!platform_poll(&physical, 2));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(!out.look_yaw && out.jump && out.held[0x39]);
    assert(!SDL_JoystickDetachVirtual(index));
    assert(!platform_poll(&physical, 3));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(physical.pad_disconnected && !out.jump && !out.held[0x39]);
    assert(out.move_speed == 640); /* Keyboard survives pad disconnect. */
    memset(&e, 0, sizeof e);
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    assert(SDL_PushEvent(&e) == 1);
    assert(!platform_poll(&physical, 4));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(physical.focus_lost && !out.move_speed);
    e.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    assert(SDL_PushEvent(&e) == 1);
    send_key(SDL_KEYDOWN, SDL_SCANCODE_W); /* A stale repeat cannot revive movement. */
    assert(!platform_poll(&physical, 5));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(!out.move_speed);
    send_key(SDL_KEYUP, SDL_SCANCODE_W);
    assert(!platform_poll(&physical, 6));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    send_key(SDL_KEYDOWN, SDL_SCANCODE_W);
    assert(!platform_poll(&physical, 7));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(out.move_speed);
    platform_shutdown();
    puts("controls SDL smoke: synthetic keyboard/mouse/pad move/look/jump/fire, disconnect and focus PASS");
    return 0;
}
