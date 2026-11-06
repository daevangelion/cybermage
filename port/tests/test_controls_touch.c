#include "controls.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void finger(Uint32 type, SDL_FingerID id, float x, float y, float dx, float dy)
{
    SDL_Event event;
    memset(&event, 0, sizeof event);
    event.type = type;
    event.tfinger.fingerId = id;
    event.tfinger.x = x / 2400;
    event.tfinger.y = y / 1080;
    event.tfinger.dx = dx / 2400;
    event.tfinger.dy = dy / 1080;
    assert(SDL_PushEvent(&event) == 1);
}

int main(void)
{
    cm_controls c;
    cm_input physical = {0}, out;
    SDL_Window *window = NULL;
    SDL_Surface *display;
    static uint8_t pixels[CM_WIDTH * CM_HEIGHT], palette[768];
    unsigned id;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(!platform_init(0));
    for (id = 1; id < 32 && !window; ++id) window = SDL_GetWindowFromID(id);
    assert(window);
    SDL_SetWindowSize(window, 2400, 1080);
    assert(!platform_controls(1, 0));
    controls_init(&c);
    assert(!platform_poll(&physical, 0));
    finger(SDL_FINGERDOWN, 1, 1200, 540, 0, 0);
    finger(SDL_FINGERUP, 1, 1200, 540, 0, 0);
    assert(!platform_poll(&physical, 1));
    controls_frame(&c, CONTROLS_MENU, &physical, &out);
    assert(out.mouse_pressed & 1); /* A complete tap between frames still clicks. */
    assert(!platform_controls(1, 1));
    finger(SDL_FINGERDOWN, 2, 240, 900, 0, 0);
    finger(SDL_FINGERMOTION, 2, 300, 850, 60, -50);
    finger(SDL_FINGERDOWN, 3, 1400, 400, 0, 0);
    finger(SDL_FINGERMOTION, 3, 1424, 421.6f, 24, 21.6f);
    finger(SDL_FINGERDOWN, 4, 2180, 730, 0, 0);
    finger(SDL_FINGERDOWN, 5, 2300, 730, 0, 0);
    assert(!platform_poll(&physical, 2));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(out.move_speed > 100 && out.move_speed <= 640);
    assert(out.look_yaw < 0 && out.look_pitch > 0);
    assert(out.held[0x39] && out.jump);
    assert(!platform_present(pixels, palette));
    display = SDL_GetWindowSurface(window);
    assert(display && display->format->BytesPerPixel == 4);
    /* Virtual stick outline lies in the left black bar, outside the 4:3 game. */
    assert(*(uint32_t *)((uint8_t *)display->pixels + 785 * display->pitch + 116 * 4) ==
           SDL_MapRGB(display->format, 160, 220, 220));
    assert(*(uint32_t *)((uint8_t *)display->pixels + 400 * display->pitch + 1000 * 4) ==
           SDL_MapRGB(display->format, 0, 0, 0));
    finger(SDL_FINGERUP, 2, 300, 850, 0, 0);
    finger(SDL_FINGERUP, 3, 1424, 421.6f, 0, 0);
    finger(SDL_FINGERUP, 4, 2180, 730, 0, 0);
    finger(SDL_FINGERUP, 5, 2300, 730, 0, 0);
    assert(!platform_poll(&physical, 3));
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(!out.move_speed && !out.jump && !out.held[0x39]);
    platform_shutdown();
    puts("controls touch: menu tap, multi-touch move/look/fire/jump, release and letterbox overlay PASS");
    return 0;
}
