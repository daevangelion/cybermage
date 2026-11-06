#define SDL_GetTicks64 lifecycle_ticks
#define SDL_Delay lifecycle_delay
#include "../src/platform_sdl.c"
#include <assert.h>

static Uint64 now = 1000;
static Uint32 waited;
Uint64 lifecycle_ticks(void) { return now; }
void lifecycle_delay(Uint32 milliseconds) { waited = milliseconds; }

int main(void)
{
    SDL_Event event;
    cm_input input = {0};
    assert(!platform_init(1));
    assert(!platform_controls(1, 0));
    is_headless = 0;
    memset(&event, 0, sizeof event);
    event.type = SDL_APP_WILLENTERBACKGROUND;
    assert(SDL_PushEvent(&event) == 1);
    now += 600000;
    event.type = SDL_APP_DIDENTERFOREGROUND;
    assert(SDL_PushEvent(&event) == 1);
    assert(!platform_poll(&input, 0));
    assert(platform_ticks(0) == 0);
    platform_wait(0);
    assert(waited == 25);
    now += 25;
    assert(platform_ticks(0) == 1);
    event.type = SDL_RENDER_DEVICE_RESET;
    assert(SDL_PushEvent(&event) == 1);
    assert(!platform_poll(&input, 1));
    assert(input.render_reset);
    assert(!platform_poll(&input, 2));
    assert(!input.render_reset);
    platform_shutdown();
    return 0;
}
