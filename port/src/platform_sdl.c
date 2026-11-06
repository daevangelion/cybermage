#include "platform.h"
#include "audio.h"

#include <SDL.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define KEY_EVENTS_MAX 4096
#define PATH_MAX_BYTES 1024

typedef struct {
    unsigned frame, scan;
    int down;
} key_event;

static cm_error error;
static SDL_Window *window;
static SDL_Surface *surface;
static SDL_AudioDeviceID device;
static uint64_t start;
static int is_headless;
static key_event events[KEY_EVENTS_MAX];
static unsigned event_count, event_next;

int cm_fail(const char *file, uint32_t offset, const char *message)
{
    error.file = file;
    error.offset = offset;
    error.message = message;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s:0x%x: %s", file, offset, message);
    return -1;
}

const cm_error *cm_last_error(void)
{
    return &error;
}

int platform_report(const char *message)
{
    SDL_Log("%s", message);
    return 0;
}

int platform_file_read(cm_file *f, const char *path)
{
    SDL_RWops *rw;
    Sint64 size;
    uint8_t *data;
    f->data = NULL;
    f->size = 0;
    rw = SDL_RWFromFile(path, "rb");
    if (!rw)
        return cm_fail(path, 0, SDL_GetError());
    size = SDL_RWsize(rw);
    if (size < 0 || size > CM_FILE_MAX) {
        SDL_RWclose(rw);
        return cm_fail(path, 0, "file exceeds platform size limit or size unavailable");
    }
    data = SDL_malloc(size ? (size_t)size : 1);
    if (!data) {
        SDL_RWclose(rw);
        return cm_fail(path, 0, "out of memory reading file");
    }
    if (SDL_RWread(rw, data, 1, (size_t)size) != (size_t)size) {
        SDL_free(data);
        SDL_RWclose(rw);
        return cm_fail(path, 0, "short file read");
    }
    if (SDL_RWclose(rw)) {
        SDL_free(data);
        return cm_fail(path, 0, "file close failed");
    }
    f->data = data;
    f->size = (uint32_t)size;
    return 0;
}

void platform_file_release(cm_file *f)
{
    SDL_free(f->data);
    f->data = NULL;
    f->size = 0;
}

static void mix_callback(void *unused, Uint8 *out, int bytes)
{
    (void)unused;
    audio_mix((int16_t *)out, (unsigned)bytes / (2 * sizeof(int16_t)));
}

int platform_init(int headless)
{
    SDL_AudioSpec spec, obtained;
    is_headless = headless;
    if (SDL_Init(SDL_INIT_TIMER | (headless ? 0 : SDL_INIT_VIDEO | SDL_INIT_AUDIO)))
        return cm_fail("SDL", 0, SDL_GetError());
    start = SDL_GetTicks64();
    if (headless)
        return audio_init(48000);
    window = SDL_CreateWindow("CyberMage", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 960, 720,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window)
        return cm_fail("SDL", 0, SDL_GetError());
    surface = SDL_CreateRGBSurfaceWithFormat(0, CM_WIDTH, CM_HEIGHT, 8,
                                             SDL_PIXELFORMAT_INDEX8);
    if (!surface)
        return cm_fail("SDL", 0, SDL_GetError());
    memset(&spec, 0, sizeof spec);
    spec.freq = 48000;
    spec.format = AUDIO_S16SYS;
    spec.channels = 2;
    spec.samples = 1024;
    spec.callback = mix_callback;
    device = SDL_OpenAudioDevice(NULL, 0, &spec, &obtained, 0);
    if (!device)
        return cm_fail("SDL audio", 0, SDL_GetError());
    if (audio_init((unsigned)obtained.freq))
        return -1;
    SDL_PauseAudioDevice(device, 0);
    return 0;
}

void platform_audio_lock(void)
{
    if (device) SDL_LockAudioDevice(device);
}

void platform_audio_unlock(void)
{
    if (device) SDL_UnlockAudioDevice(device);
}

void platform_shutdown(void)
{
    if (device) SDL_CloseAudioDevice(device);
    if (surface) SDL_FreeSurface(surface);
    if (window) SDL_DestroyWindow(window);
    device = 0;
    surface = NULL;
    window = NULL;
    SDL_Quit();
}

int platform_keys(const char *path)
{
    cm_file file = {0};
    uint32_t at = 0;
    event_count = event_next = 0;
    if (platform_file_read(&file, path))
        return -1;
    while (at < file.size) {
        char line[128], action[8];
        unsigned n = 0, frame, scan;
        int used = 0;
        uint32_t offset = at;
        while (at < file.size && file.data[at] != '\n') {
            if (n + 1 >= sizeof line) {
                platform_file_release(&file);
                return cm_fail(path, offset, "input line too long");
            }
            line[n++] = (char)file.data[at++];
        }
        if (at < file.size) ++at;
        line[n] = 0;
        if (!n || line[0] == '#') continue;
        if (sscanf(line, "%u %7s %x %n", &frame, action, &scan, &used) != 3 ||
            line[used] || scan == 0 || scan >= CM_KEY_COUNT ||
            (strcmp(action, "down") && strcmp(action, "up")) ||
            event_count == KEY_EVENTS_MAX ||
            (event_count && frame < events[event_count - 1].frame)) {
            platform_file_release(&file);
            return cm_fail(path, offset, "expected sorted: FRAME down|up HEX_SCAN");
        }
        events[event_count].frame = frame;
        events[event_count].scan = scan;
        events[event_count++].down = !strcmp(action, "down");
    }
    platform_file_release(&file);
    return 0;
}

static unsigned dos_scan(SDL_Scancode sc)
{
    static const SDL_Scancode map[CM_KEY_COUNT] = {
        [1]=SDL_SCANCODE_ESCAPE, [2]=SDL_SCANCODE_1, [3]=SDL_SCANCODE_2,
        [4]=SDL_SCANCODE_3, [5]=SDL_SCANCODE_4, [6]=SDL_SCANCODE_5,
        [7]=SDL_SCANCODE_6, [8]=SDL_SCANCODE_7, [9]=SDL_SCANCODE_8,
        [10]=SDL_SCANCODE_9, [11]=SDL_SCANCODE_0, [12]=SDL_SCANCODE_MINUS,
        [13]=SDL_SCANCODE_EQUALS, [14]=SDL_SCANCODE_BACKSPACE, [15]=SDL_SCANCODE_TAB,
        [16]=SDL_SCANCODE_Q, [17]=SDL_SCANCODE_W, [18]=SDL_SCANCODE_E,
        [19]=SDL_SCANCODE_R, [20]=SDL_SCANCODE_T, [21]=SDL_SCANCODE_Y,
        [22]=SDL_SCANCODE_U, [23]=SDL_SCANCODE_I, [24]=SDL_SCANCODE_O,
        [25]=SDL_SCANCODE_P, [26]=SDL_SCANCODE_LEFTBRACKET,
        [27]=SDL_SCANCODE_RIGHTBRACKET, [28]=SDL_SCANCODE_RETURN,
        [29]=SDL_SCANCODE_LCTRL, [30]=SDL_SCANCODE_A, [31]=SDL_SCANCODE_S,
        [32]=SDL_SCANCODE_D, [33]=SDL_SCANCODE_F, [34]=SDL_SCANCODE_G,
        [35]=SDL_SCANCODE_H, [36]=SDL_SCANCODE_J, [37]=SDL_SCANCODE_K,
        [38]=SDL_SCANCODE_L, [39]=SDL_SCANCODE_SEMICOLON,
        [40]=SDL_SCANCODE_APOSTROPHE, [41]=SDL_SCANCODE_GRAVE,
        [42]=SDL_SCANCODE_LSHIFT, [43]=SDL_SCANCODE_BACKSLASH,
        [44]=SDL_SCANCODE_Z, [45]=SDL_SCANCODE_X, [46]=SDL_SCANCODE_C,
        [47]=SDL_SCANCODE_V, [48]=SDL_SCANCODE_B, [49]=SDL_SCANCODE_N,
        [50]=SDL_SCANCODE_M, [51]=SDL_SCANCODE_COMMA, [52]=SDL_SCANCODE_PERIOD,
        [53]=SDL_SCANCODE_SLASH, [54]=SDL_SCANCODE_RSHIFT,
        [55]=SDL_SCANCODE_KP_MULTIPLY, [56]=SDL_SCANCODE_LALT,
        [57]=SDL_SCANCODE_SPACE, [58]=SDL_SCANCODE_CAPSLOCK,
        [59]=SDL_SCANCODE_F1, [60]=SDL_SCANCODE_F2, [61]=SDL_SCANCODE_F3,
        [62]=SDL_SCANCODE_F4, [63]=SDL_SCANCODE_F5, [64]=SDL_SCANCODE_F6,
        [65]=SDL_SCANCODE_F7, [66]=SDL_SCANCODE_F8, [67]=SDL_SCANCODE_F9,
        [68]=SDL_SCANCODE_F10, [69]=SDL_SCANCODE_NUMLOCKCLEAR,
        [70]=SDL_SCANCODE_SCROLLLOCK, [71]=SDL_SCANCODE_HOME,
        [72]=SDL_SCANCODE_UP, [73]=SDL_SCANCODE_PAGEUP,
        [74]=SDL_SCANCODE_KP_MINUS, [75]=SDL_SCANCODE_LEFT,
        [76]=SDL_SCANCODE_KP_5, [77]=SDL_SCANCODE_RIGHT,
        [78]=SDL_SCANCODE_KP_PLUS, [79]=SDL_SCANCODE_END,
        [80]=SDL_SCANCODE_DOWN, [81]=SDL_SCANCODE_PAGEDOWN,
        [82]=SDL_SCANCODE_INSERT, [83]=SDL_SCANCODE_DELETE,
        [87]=SDL_SCANCODE_F11, [88]=SDL_SCANCODE_F12
    };
    unsigned i;
    if (sc == SDL_SCANCODE_RCTRL) return 29;
    if (sc == SDL_SCANCODE_RALT) return 56;
    if (sc == SDL_SCANCODE_KP_ENTER) return 28;
    for (i = 1; i < CM_KEY_COUNT; ++i)
        if (map[i] == sc && sc != SDL_SCANCODE_UNKNOWN)
            return i;
    return 0;
}

static void key_set(cm_input *input, unsigned scan, int down)
{
    if (scan && input->held[scan] != down) {
        input->held[scan] = (uint8_t)down;
        if (down) input->pressed[scan] = 1;
        else input->released[scan] = 1;
    }
}

int platform_poll(cm_input *input, unsigned frame)
{
    SDL_Event e;
    memset(input->pressed, 0, sizeof input->pressed);
    memset(input->released, 0, sizeof input->released);
    input->mouse_dx = input->mouse_dy = 0;
    if (!is_headless) while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: input->quit = 1; break;
        case SDL_KEYDOWN: case SDL_KEYUP:
            key_set(input, dos_scan(e.key.keysym.scancode), e.type == SDL_KEYDOWN);
            break;
        case SDL_MOUSEMOTION: {
            int w, h;
            SDL_GetWindowSize(window, &w, &h);
            if (w <= 0 || h <= 0) break;
            input->mouse_x = e.motion.x * CM_WIDTH / w;
            input->mouse_y = e.motion.y * CM_HEIGHT / h;
            input->mouse_dx += e.motion.xrel;
            input->mouse_dy += e.motion.yrel;
            break;
        }
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
            input->mouse_buttons = SDL_GetMouseState(NULL, NULL);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                unsigned i;
                for (i = 1; i < CM_KEY_COUNT; ++i) key_set(input, i, 0);
                input->mouse_buttons = 0;
            }
            break;
        default: break;
        }
    }
    while (event_next < event_count && events[event_next].frame <= frame) {
        key_event *k = &events[event_next++];
        key_set(input, k->scan, k->down);
    }
    return 0;
}

int platform_present(const uint8_t *pixels, const uint8_t *palette)
{
    SDL_Color colors[256];
    SDL_Surface *dest;
    unsigned i, row;
    if (is_headless) return 0;
    for (i = 0; i < 256; ++i) {
        colors[i].r = palette[i*3];
        colors[i].g = palette[i*3+1];
        colors[i].b = palette[i*3+2];
        colors[i].a = SDL_ALPHA_OPAQUE;
    }
    if (SDL_SetPaletteColors(surface->format->palette, colors, 0, 256))
        return cm_fail("SDL", 0, SDL_GetError());
    for (row = 0; row < CM_HEIGHT; ++row)
        memcpy((uint8_t *)surface->pixels + row * surface->pitch,
               pixels + row * CM_WIDTH, CM_WIDTH);
    dest = SDL_GetWindowSurface(window);
    if (!dest || SDL_BlitScaled(surface, NULL, dest, NULL) ||
        SDL_UpdateWindowSurface(window))
        return cm_fail("SDL", 0, SDL_GetError());
    return 0;
}

static int make_directory(const char *path)
{
    char buf[PATH_MAX_BYTES];
    size_t i, n = strlen(path);
    if (!n || n >= sizeof buf)
        return cm_fail(path, 0, "dump directory path too long or empty");
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; ++i) {
        if (buf[i] != '/' && buf[i]) continue;
        buf[i] = 0;
        if (mkdir(buf, 0755) && errno != EEXIST)
            return cm_fail(path, 0, "cannot create dump directory");
        buf[i] = path[i];
    }
    return 0;
}

static int write_bytes(const char *path, const uint8_t *data, size_t size)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "wb");
    if (!rw) return cm_fail(path, 0, SDL_GetError());
    if (SDL_RWwrite(rw, data, 1, size) != size) {
        SDL_RWclose(rw);
        return cm_fail(path, 0, "short frame write");
    }
    if (SDL_RWclose(rw)) return cm_fail(path, 0, "frame close failed");
    return 0;
}

int platform_dump(const char *directory, unsigned frame,
                  const uint8_t *pixels, const uint8_t *palette)
{
    char path[PATH_MAX_BYTES];
    static uint8_t ppm[CM_WIDTH * CM_HEIGHT * 3 + 32];
    const char header[] = "P6\n640 480\n255\n";
    unsigned i;
    int n;
    if (make_directory(directory)) return -1;
    n = snprintf(path, sizeof path, "%s/frame%06u.idx", directory, frame);
    if (n < 0 || (size_t)n >= sizeof path)
        return cm_fail(directory, 0, "frame path too long");
    if (write_bytes(path, pixels, CM_WIDTH * CM_HEIGHT)) return -1;
    n = snprintf(path, sizeof path, "%s/frame%06u.pal", directory, frame);
    if (n < 0 || (size_t)n >= sizeof path)
        return cm_fail(directory, 0, "frame path too long");
    if (write_bytes(path, palette, 768)) return -1;
    memcpy(ppm, header, sizeof header - 1);
    for (i = 0; i < CM_WIDTH * CM_HEIGHT; ++i)
        memcpy(ppm + sizeof header - 1 + i * 3, palette + pixels[i] * 3, 3);
    n = snprintf(path, sizeof path, "%s/frame%06u.ppm", directory, frame);
    if (n < 0 || (size_t)n >= sizeof path)
        return cm_fail(directory, 0, "frame path too long");
    return write_bytes(path, ppm, sizeof header - 1 + CM_WIDTH * CM_HEIGHT * 3);
}

void platform_wait(unsigned frame)
{
    uint64_t now = SDL_GetTicks64();
    uint64_t target = start + (uint64_t)(frame + 1) * 1000 / CM_TICK_HZ;
    if (!is_headless && now < target) SDL_Delay((Uint32)(target - now));
}
