#include "platform.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void finger(Uint32 type, SDL_FingerID id, float x, float y)
{
    SDL_Event event;
    memset(&event, 0, sizeof event);
    event.type = type;
    event.tfinger.fingerId = id;
    event.tfinger.x = x;
    event.tfinger.y = y;
    event.tfinger.pressure = type == SDL_FINGERUP ? 0.0f : 1.0f;
    assert(SDL_PushEvent(&event) == 1);
}

int main(void)
{
    cm_input input = {0};
    SDL_Window *window = NULL;
    SDL_Surface *display;
    static uint8_t pixels[CM_WIDTH * CM_HEIGHT], palette[768];
    Uint32 id;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(platform_init(0) == 0);
    for (id = 1; id < 32 && !window; ++id) window = SDL_GetWindowFromID(id);
    assert(window);
    SDL_SetWindowSize(window, 1280, 720);
    memset(pixels, 1, sizeof pixels);
    palette[3] = palette[4] = palette[5] = 252;
    assert(platform_present(pixels, palette) == 0);
    display = SDL_GetWindowSurface(window);
    assert(display && display->format->BytesPerPixel == 4);
    assert(*(uint32_t *)display->pixels == SDL_MapRGB(display->format, 0, 0, 0));
    assert(*(uint32_t *)((uint8_t *)display->pixels + 640 * 4) ==
           SDL_MapRGB(display->format, 252, 252, 252));
    assert(platform_poll(&input, 0) == 0);
    finger(SDL_FINGERDOWN, 42, 0.5f, 0.5f);
    assert(platform_poll(&input, 1) == 0);
    assert(input.touch_count == 1 && input.touches[0].id == 42);
    assert(input.touches[0].phase == CM_TOUCH_DOWN && input.touches[0].inside);
    assert(input.mouse_x == 320 && input.mouse_y == 240);
    assert((input.mouse_buttons & 1) && (input.mouse_pressed & 1));
    finger(SDL_FINGERUP, 42, 0.5f, 0.5f);
    assert(platform_poll(&input, 2) == 0);
    assert(input.mouse_released & 1);
    assert(!(input.mouse_buttons & 1));
    finger(SDL_FINGERDOWN, 43, 0.5f, 0.5f);
    finger(SDL_FINGERUP, 43, 0.5f, 0.5f);
    assert(platform_poll(&input, 3) == 0);
    assert(input.touch_count == 2 && (input.mouse_pressed & input.mouse_released & 1));
    finger(SDL_FINGERDOWN, 44, 0.0f, 0.5f);
    assert(platform_poll(&input, 4) == 0);
    assert(input.touch_count == 1 && !input.touches[0].inside);
    assert(!(input.mouse_buttons & 1));
    for (id = 0; id < CM_RAW_SCANS_MAX + 8; ++id) {
        SDL_Event key;
        memset(&key, 0, sizeof key);
        key.type = SDL_KEYDOWN;
        key.key.keysym.scancode = SDL_SCANCODE_A;
        key.key.repeat = id != 0;
        assert(SDL_PushEvent(&key) == 1);
    }
    {
        SDL_Event key;
        memset(&key, 0, sizeof key);
        key.type = SDL_KEYUP;
        key.key.keysym.scancode = SDL_SCANCODE_A;
        assert(SDL_PushEvent(&key) == 1);
    }
    assert(platform_poll(&input, 5) == 0);
    assert(input.raw_count <= CM_RAW_SCANS_MAX && !input.held[0x1e]);
    platform_shutdown();
    puts("touch: 4:3 viewport coordinates, menu click edges and letterbox rejection passed");
    return 0;
}
