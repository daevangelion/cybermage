#define SDL_GetTicks64 lifecycle_ticks
#define SDL_Delay lifecycle_delay
#define fsync lifecycle_sync
#include "../src/platform_sdl.c"
#include <assert.h>

static Uint64 now = 1000;
static Uint32 waited;
static int sync_failure, sync_calls;
Uint64 lifecycle_ticks(void) { return now; }
void lifecycle_delay(Uint32 milliseconds) { waited = milliseconds; }
int lifecycle_sync(int fd)
{
    assert(fd >= 0);
    ++sync_calls;
    return sync_failure ? -1 : 0;
}

int main(int argc, char **argv)
{
    SDL_Event event;
    cm_input input = {0};
    cm_file file = {0};
    assert(argc == 2);
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
    gl_context = (SDL_GLContext)(uintptr_t)1; /* SDL replaced an old handle. */
    event.type = SDL_RENDER_DEVICE_RESET;
    assert(SDL_PushEvent(&event) == 1);
    assert(!platform_poll(&input, 1));
    assert(input.render_reset);
    assert(gl_context == SDL_GL_GetCurrentContext());
    assert(!platform_poll(&input, 2));
    assert(!input.render_reset);
    assert(!platform_file_write(argv[1], (const uint8_t *)"old", 3));
    sync_failure = 1;
    assert(platform_file_write(argv[1], (const uint8_t *)"new", 3) == -1);
    assert(sync_calls == 2);
    assert(!platform_file_read(&file, argv[1]));
    assert(file.size == 3 && !memcmp(file.data, "old", 3));
    platform_file_release(&file);
    platform_shutdown();
    return 0;
}
