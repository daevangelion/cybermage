#include "platform.h"
#include "audio.h"
#include "controls.h"

#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define KEY_EVENTS_MAX 4096
#define PATH_MAX_BYTES 1024

typedef struct {
    unsigned frame, scan;
    int down;
} key_event;

static cm_error error;
static char error_file[PATH_MAX_BYTES], error_message[512];
static SDL_Window *window;
static SDL_GLContext gl_context;
static int gl_window;
static SDL_Surface *surface;
static SDL_Surface *converted;
static SDL_AudioDeviceID device;
static uint64_t start;
static int is_headless;
static key_event events[KEY_EVENTS_MAX];
static unsigned event_count, event_next;
static uint8_t extended_held[CM_KEY_COUNT];
static SDL_FingerID primary_finger;
static int primary_down;
static char *user_dir;
static SDL_GameController *controller;
static SDL_JoystickID controller_id = -1;
static int modern_controls, gameplay_controls, input_focus = 1;
static int touch_visible;
static double touch_look_fraction_x, touch_look_fraction_y;
static uint8_t focus_keys[CM_KEY_COUNT], focus_axes[6];
static uint32_t focus_mouse, focus_pad;

#define TOUCH_FINGERS_MAX 10
typedef struct {
    SDL_FingerID id;
    int active, role, x, y, origin_x, origin_y;
    unsigned action;
} touch_finger;
static touch_finger fingers[TOUCH_FINGERS_MAX];

static void controller_open(int index)
{
    if (controller || !SDL_IsGameController(index)) return;
    controller = SDL_GameControllerOpen(index);
    if (controller)
        controller_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
}

int platform_controls(int modern, int gameplay)
{
    int relative = modern && gameplay && input_focus;
    if (modern && !modern_controls) {
        int i;
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS))
            return cm_fail("SDL controller", 0, SDL_GetError());
        for (i = 0; i < SDL_NumJoysticks(); ++i) controller_open(i);
    }
    modern_controls = modern;
    gameplay_controls = modern && gameplay;
#ifdef __ANDROID__
    if (modern) touch_visible = 1;
#endif
    if (!is_headless && SDL_GetRelativeMouseMode() != (relative ? SDL_TRUE : SDL_FALSE)) {
#ifndef __ANDROID__
        if (SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE))
            return cm_fail("SDL relative mouse", 0, SDL_GetError());
#endif
    }
    return 0;
}

int cm_fail(const char *file, uint32_t offset, const char *message)
{
    SDL_strlcpy(error_file, file, sizeof error_file);
    SDL_strlcpy(error_message, message, sizeof error_message);
    error.file = error_file;
    error.offset = offset;
    error.message = error_message;
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

int platform_file_exists(const char *path)
{
    struct stat st;
    if (!stat(path, &st)) {
        if (S_ISREG(st.st_mode)) return 1;
        return cm_fail(path, 0, "expected a regular file");
    }
    if (errno == ENOENT) return 0;
    return cm_fail(path, 0, strerror(errno));
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
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    flags = SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
    is_headless = headless;
    if (SDL_Init(SDL_INIT_TIMER | (headless ? 0 : SDL_INIT_VIDEO | SDL_INIT_AUDIO)))
        return cm_fail("SDL", 0, SDL_GetError());
    user_dir = SDL_GetPrefPath("jshield", "cybermage");
    if (!user_dir) return cm_fail("SDL user directory", 0, SDL_GetError());
    start = SDL_GetTicks64();
    if (headless)
        return audio_init(48000);
    window = SDL_CreateWindow("CyberMage", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 960, 720,
                              flags);
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
    if (gl_context) SDL_GL_DeleteContext(gl_context);
    gl_context = NULL;
    gl_window = 0;
    if (controller) SDL_GameControllerClose(controller);
    controller = NULL;
    controller_id = -1;
    modern_controls = gameplay_controls = touch_visible = 0;
    input_focus = 1;
    memset(fingers, 0, sizeof fingers);
    touch_look_fraction_x = touch_look_fraction_y = 0;
    memset(focus_keys, 0, sizeof focus_keys);
    memset(focus_axes, 0, sizeof focus_axes);
    focus_mouse = focus_pad = 0;
    if (device) SDL_CloseAudioDevice(device);
    if (surface) SDL_FreeSurface(surface);
    if (converted) SDL_FreeSurface(converted);
    if (window) SDL_DestroyWindow(window);
    device = 0;
    surface = NULL;
    converted = NULL;
    window = NULL;
    SDL_free(user_dir);
    user_dir = NULL;
    SDL_Quit();
}

const char *platform_user_dir(void)
{
    return user_dir;
}

static SDL_Rect viewport(int w, int h)
{
    SDL_Rect rect = {0, 0, w, h};
    if ((int64_t)w * CM_HEIGHT <= (int64_t)h * CM_WIDTH)
        rect.h = w * CM_HEIGHT / CM_WIDTH;
    else
        rect.w = h * CM_WIDTH / CM_HEIGHT;
    rect.x = (w - rect.w) / 2;
    rect.y = (h - rect.h) / 2;
    return rect;
}

static int pointer_point(int x, int y, int *game_x, int *game_y)
{
    SDL_Rect rect;
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    rect = viewport(w, h);
    if (!rect.w || !rect.h) return 0;
    *game_x = (x - rect.x) * CM_WIDTH / rect.w;
    *game_y = (y - rect.y) * CM_HEIGHT / rect.h;
    return x >= rect.x && y >= rect.y &&
           x < rect.x + rect.w && y < rect.y + rect.h;
}

static void mouse_button(cm_input *input, unsigned bit, int down)
{
    if (down) {
        if (!(input->mouse_buttons & bit)) input->mouse_pressed |= bit;
        input->mouse_buttons |= bit;
    } else {
        if (input->mouse_buttons & bit) input->mouse_released |= bit;
        input->mouse_buttons &= ~bit;
    }
}

static void touch_layout(int w, int h, SDL_Rect *stick, SDL_Rect buttons[6])
{
    SDL_Rect view = viewport(w, h);
    int unit = h / 480 + 1;
    int side = h * 50 / 480, diameter = h * 110 / 480;
    int right = w - view.x;
    unsigned i;
    if (side < 24) side = 24;
    if (diameter < 48) diameter = 48;
    if (view.x >= 64 * unit && diameter > view.x - unit * 8)
        diameter = view.x - unit * 8;
    stick->w = stick->h = diameter;
    stick->x = view.x >= diameter + unit * 8 ?
        (view.x - diameter) / 2 : view.x + unit * 8;
    stick->y = h - diameter - unit * 16;
    if (view.x >= 2 * side + unit * 12) right = w - unit * 6;
    else right -= unit * 6;
    for (i = 0; i < 6; ++i) {
        buttons[i].w = buttons[i].h = side;
        if (view.x >= side + unit * 12 && view.x < 2 * side + unit * 12) {
            buttons[i].x = w - (view.x + side) / 2;
            buttons[i].y = h - (6 - (int)i) * (side + unit * 6);
        } else {
            buttons[i].x = right - (2 - (int)(i % 2)) * (side + unit * 4);
            buttons[i].y = h - (3 - (int)(i / 2)) * (side + unit * 6);
        }
    }
}

static int in_rect(int x, int y, const SDL_Rect *r)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static int modern_touch(cm_input *input, const SDL_TouchFingerEvent *event,
                         int w, int h)
{
    static const unsigned actions[6] = {
        CM_CONTROL_FIRE, CM_CONTROL_JUMP, CM_CONTROL_USE,
        CM_CONTROL_NEXT, CM_CONTROL_MENU, CM_CONTROL_AIM
    };
    SDL_Rect stick, buttons[6];
    touch_finger *finger = NULL;
    unsigned i;
    int x = (int)(event->x * w), y = (int)(event->y * h);
    for (i = 0; i < TOUCH_FINGERS_MAX; ++i)
        if (fingers[i].active && fingers[i].id == event->fingerId) finger = &fingers[i];
    if (event->type == SDL_FINGERDOWN && !finger) {
        for (i = 0; i < TOUCH_FINGERS_MAX; ++i)
            if (!fingers[i].active) { finger = &fingers[i]; break; }
        if (!finger) return cm_fail("touch", 0, "more than ten active fingers");
        memset(finger, 0, sizeof *finger);
        finger->active = 1;
        finger->id = event->fingerId;
        finger->role = gameplay_controls ? 0 : 1;
        finger->origin_x = x;
        finger->origin_y = y;
        if (gameplay_controls) {
            touch_layout(w, h, &stick, buttons);
            if (in_rect(x, y, &stick)) finger->role = 2;
            else {
                for (i = 0; i < 6; ++i) if (in_rect(x, y, &buttons[i])) {
                    finger->role = 4;
                    finger->action = actions[i];
                    input->touch_pressed |= 1u << finger->action;
                    break;
                }
                if (!finger->role && x >= w / 2) finger->role = 3;
            }
        }
    }
    if (!finger) return 1;
    finger->x = x;
    finger->y = y;
    if (finger->role == 3 && event->type == SDL_FINGERMOTION && gameplay_controls) {
        int dx, dy;
        touch_look_fraction_x += event->dx * 640;
        touch_look_fraction_y += event->dy * 480;
        dx = (int)touch_look_fraction_x;
        dy = (int)touch_look_fraction_y;
        input->touch_look_dx += dx;
        input->touch_look_dy += dy;
        touch_look_fraction_x -= dx;
        touch_look_fraction_y -= dy;
    }
    i = (unsigned)finger->role;
    if (event->type == SDL_FINGERUP) finger->active = 0;
    touch_visible = 1;
    return i == 1 ? 0 : 1;
}

static void touch_sample(cm_input *input)
{
    unsigned i;
    int w, h;
    SDL_Rect stick, buttons[6];
    input->touch_move_x = input->touch_move_y = 0;
    input->touch_buttons = 0;
    if (!window) return;
    SDL_GetWindowSize(window, &w, &h);
    touch_layout(w, h, &stick, buttons);
    for (i = 0; i < TOUCH_FINGERS_MAX; ++i) {
        const touch_finger *f = &fingers[i];
        if (!f->active) continue;
        if (f->role == 4) input->touch_buttons |= 1u << f->action;
        else if (f->role == 2) {
            int64_t x = (int64_t)(f->x - f->origin_x) * 65534 / stick.w;
            int64_t y = (int64_t)(f->y - f->origin_y) * 65534 / stick.h;
            input->touch_move_x = (int16_t)(x < -32767 ? -32767 : x > 32767 ? 32767 : x);
            input->touch_move_y = (int16_t)(y < -32767 ? -32767 : y > 32767 ? 32767 : y);
        }
    }
}

static int touch_event(cm_input *input, const SDL_TouchFingerEvent *finger)
{
    cm_touch *touch;
    SDL_Rect rect;
    int w, h;
    if (input->touch_count == CM_TOUCH_MAX)
        return cm_fail("input", 0, "touch event queue overflow");
    SDL_GetWindowSize(window, &w, &h);
    rect = viewport(w, h);
    if (!rect.w || !rect.h) return 0;
    if (modern_controls) {
        int result = modern_touch(input, finger, w, h);
        if (result < 0) return -1;
        if (result) return 0;
    }
    touch = &input->touches[input->touch_count++];
    touch->id = finger->fingerId;
    touch->inside = (uint8_t)pointer_point((int)(finger->x * w),
                                         (int)(finger->y * h),
                                         &touch->x, &touch->y);
    touch->dx = (int)(finger->dx * w * CM_WIDTH / rect.w);
    touch->dy = (int)(finger->dy * h * CM_HEIGHT / rect.h);
    touch->pressure = finger->pressure <= 0 ? 0 :
                      finger->pressure >= 1 ? 65535 :
                      (uint16_t)(finger->pressure * 65535);
    touch->phase = finger->type == SDL_FINGERDOWN ? CM_TOUCH_DOWN :
                   finger->type == SDL_FINGERUP ? CM_TOUCH_UP : CM_TOUCH_MOVE;
    if (touch->phase == CM_TOUCH_DOWN && !primary_down && touch->inside) {
        primary_finger = finger->fingerId;
        primary_down = 1;
        input->mouse_x = touch->x;
        input->mouse_y = touch->y;
        mouse_button(input, SDL_BUTTON_LMASK, 1);
    } else if (primary_down && primary_finger == finger->fingerId) {
        input->mouse_x = touch->x;
        input->mouse_y = touch->y;
        input->mouse_dx += touch->dx;
        input->mouse_dy += touch->dy;
        if (touch->phase == CM_TOUCH_UP) {
            mouse_button(input, SDL_BUTTON_LMASK, 0);
            primary_down = 0;
        }
    }
    return 0;
}

static int input_number(const char **p, unsigned base, unsigned limit, unsigned *v)
{
    unsigned long n;
    char *end;
    while (isspace((unsigned char)**p)) ++*p;
    if (!**p || **p == '-' || **p == '+') return -1;
    errno = 0;
    n = strtoul(*p, &end, (int)base);
    if (end == *p || errno == ERANGE || n > limit) return -1;
    *p = end;
    *v = (unsigned)n;
    return 0;
}

int platform_keys(const char *path)
{
    cm_file file = {0};
    uint32_t at = 0;
    event_count = event_next = 0;
    if (platform_file_read(&file, path))
        return -1;
    while (at < file.size) {
        char line[128];
        const char *p;
        unsigned n = 0, frame, scan;
        int down;
        uint32_t offset = at;
        while (at < file.size && file.data[at] != '\n') {
            if (n + 1 >= sizeof line || !file.data[at]) {
                platform_file_release(&file);
                return cm_fail(path, offset, "input line too long or contains NUL");
            }
            line[n++] = (char)file.data[at++];
        }
        if (at < file.size) ++at;
        line[n] = 0;
        if (!n || line[0] == '#') continue;
        p = line;
        if (input_number(&p, 10, 1000000, &frame) || !isspace((unsigned char)*p))
            goto bad;
        while (isspace((unsigned char)*p)) ++p;
        if (!strncmp(p, "down", 4) && isspace((unsigned char)p[4])) {
            down = 1;
            p += 4;
        } else if (!strncmp(p, "up", 2) && isspace((unsigned char)p[2])) {
            down = 0;
            p += 2;
        } else if (!strncmp(p, "raw", 3) && isspace((unsigned char)p[3])) {
            down = -1;
            p += 3;
        } else goto bad;
        if (input_number(&p, 16, down < 0 ? 255 : 0xe07f, &scan)) goto bad;
        while (isspace((unsigned char)*p)) ++p;
        if (*p || (down >= 0 && (!scan ||
             (scan >= 128 && (scan < 0xe001 || scan > 0xe07f)))) ||
            event_count == KEY_EVENTS_MAX ||
            (event_count && frame < events[event_count - 1].frame)) {
bad:
            platform_file_release(&file);
            return cm_fail(path, offset, "expected sorted: FRAME down|up HEX_SCAN or FRAME raw HEX_BYTE");
        }
        events[event_count].frame = frame;
        events[event_count].scan = scan;
        events[event_count++].down = down;
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
    if (sc == SDL_SCANCODE_AC_BACK) return 1;
    if (sc == SDL_SCANCODE_RCTRL) return 29;
    if (sc == SDL_SCANCODE_RALT) return 56;
    if (sc == SDL_SCANCODE_KP_ENTER) return 28;
    if (sc == SDL_SCANCODE_KP_DIVIDE) return 53;
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) {
        static const uint8_t keypad[9] = {79,80,81,75,76,77,71,72,73};
        return keypad[sc - SDL_SCANCODE_KP_1];
    }
    if (sc == SDL_SCANCODE_KP_0) return 82;
    if (sc == SDL_SCANCODE_KP_PERIOD) return 83;
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

static int raw_scan(cm_input *input, unsigned scan)
{
    if (input->raw_count == CM_RAW_SCANS_MAX)
        return cm_fail("input", 0, "raw scan queue overflow");
    input->raw_scans[input->raw_count++] = (uint8_t)scan;
    if (scan < 0xe0) /* CM_HI.EXE CM_ISR9 0x67B94: every >=E0 is a prefix */
        key_set(input, scan & 127, !(scan & 128));
    return 0;
}

static int emit_key(cm_input *input, unsigned code, int down)
{
    extended_held[code & 127] = (uint8_t)(down && code > 255);
    if (code > 255 && raw_scan(input, 0xe0)) return -1;
    return raw_scan(input, (code & 127) | (down ? 0 : 128));
}

static int extended_key(SDL_Scancode key)
{
    switch (key) {
    case SDL_SCANCODE_HOME: case SDL_SCANCODE_UP: case SDL_SCANCODE_PAGEUP:
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_END:
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_INSERT:
    case SDL_SCANCODE_DELETE: case SDL_SCANCODE_RCTRL: case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_KP_DIVIDE: return 1;
    default: return 0;
    }
}

int platform_poll(cm_input *input, unsigned frame)
{
    SDL_Event e;
    memset(input->pressed, 0, sizeof input->pressed);
    memset(input->released, 0, sizeof input->released);
    input->raw_count = 0;
    input->touch_count = 0;
    input->mouse_pressed = input->mouse_released = 0;
    input->mouse_dx = input->mouse_dy = 0;
    input->mouse_wheel = 0;
    input->touch_look_dx = input->touch_look_dy = 0;
    input->focus_lost = input->pad_disconnected = 0;
    input->pad_pressed = input->pad_released = input->touch_pressed = 0;
    if (!is_headless || modern_controls) while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: input->quit = 1; break;
        case SDL_KEYDOWN: case SDL_KEYUP: {
            unsigned code = dos_scan(e.key.keysym.scancode);
            if (modern_controls && code) {
                if (e.type == SDL_KEYUP) focus_keys[code] = 0;
                else if (!input_focus || focus_keys[code]) break;
            }
            if (code && extended_key(e.key.keysym.scancode)) code |= 0xe000;
            if (e.type == SDL_KEYDOWN && e.key.repeat &&
                input->raw_count + (code > 255 ? 2u : 1u) > CM_RAW_SCANS_MAX - 2)
                break;
            if (code && emit_key(input, code, e.type == SDL_KEYDOWN)) return -1;
            break;
        }
        case SDL_MOUSEMOTION: {
            if (modern_controls && !input_focus) break;
            if (window) pointer_point(e.motion.x, e.motion.y, &input->mouse_x, &input->mouse_y);
            input->mouse_dx += e.motion.xrel;
            input->mouse_dy += e.motion.yrel;
            break;
        }
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
            int inside = !window || pointer_point(e.button.x, e.button.y,
                                                  &input->mouse_x, &input->mouse_y);
            if (modern_controls && e.button.button && e.button.button <= 32) {
                uint32_t bit = 1u << (e.button.button - 1);
                if (e.type == SDL_MOUSEBUTTONUP) focus_mouse &= ~bit;
                else if (!input_focus || (focus_mouse & bit)) break;
            }
            if (e.button.button && e.button.button <= 32 &&
                (inside || e.type == SDL_MOUSEBUTTONUP))
                mouse_button(input, 1u << (e.button.button - 1),
                             e.type == SDL_MOUSEBUTTONDOWN);
            break;
        }
        case SDL_MOUSEWHEEL:
            if (modern_controls) input->mouse_wheel += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ?
                -e.wheel.y : e.wheel.y;
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (modern_controls) controller_open(e.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (modern_controls && e.cdevice.which == controller_id) {
                SDL_GameControllerClose(controller);
                controller = NULL;
                controller_id = -1;
                input->pad_disconnected = 1;
                input->pad_released |= input->pad_buttons;
                input->pad_buttons = 0;
                input->pad_pressed = 0;
                memset(input->pad_axes, 0, sizeof input->pad_axes);
                memset(focus_axes, 0, sizeof focus_axes);
                focus_pad = 0;
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (modern_controls && e.caxis.which == controller_id && e.caxis.axis < 6) {
                unsigned axis = e.caxis.axis;
                int value = e.caxis.value;
                if (focus_axes[axis]) {
                    if (axis < 4 ? value >= -5898 && value <= 5898 : value <= 8192)
                        focus_axes[axis] = 0;
                    else break;
                }
                if (input_focus) input->pad_axes[axis] = e.caxis.value;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
            if (modern_controls && e.cbutton.which == controller_id &&
                e.cbutton.button < 21) {
                uint32_t bit = 1u << e.cbutton.button;
                if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                    if (!input_focus || (focus_pad & bit)) break;
                    if (!(input->pad_buttons & bit)) input->pad_pressed |= bit;
                    input->pad_buttons |= bit;
                } else {
                    focus_pad &= ~bit;
                    if (input->pad_buttons & bit) input->pad_released |= bit;
                    input->pad_buttons &= ~bit;
                }
            }
            break;
        case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
            if (touch_event(input, &e.tfinger)) return -1;
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                unsigned i;
                if (modern_controls) {
                    for (i = 0; i < CM_KEY_COUNT; ++i) focus_keys[i] |= input->held[i];
                    focus_mouse |= input->mouse_buttons;
                    focus_pad |= input->pad_buttons;
                    for (i = 0; i < 6; ++i) {
                        int value = input->pad_axes[i];
                        if (i < 4 ? value < -5898 || value > 5898 : value > 8192)
                            focus_axes[i] = 1;
                    }
                }
                for (i = 1; i < CM_KEY_COUNT; ++i)
                    if (input->held[i] &&
                        emit_key(input, i | (extended_held[i] ? 0xe000 : 0), 0))
                        return -1;
                input->mouse_released |= input->mouse_buttons;
                input->mouse_buttons = 0;
                primary_down = 0;
                if (modern_controls) {
                    input->focus_lost = 1;
                    input_focus = 0;
                    input->pad_buttons = input->touch_buttons = 0;
                    memset(input->pad_axes, 0, sizeof input->pad_axes);
                    memset(fingers, 0, sizeof fingers);
                    touch_look_fraction_x = touch_look_fraction_y = 0;
                    if (platform_controls(1, gameplay_controls)) return -1;
                }
            } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && modern_controls) {
                input_focus = 1;
                if (platform_controls(1, gameplay_controls)) return -1;
            }
            break;
        default: break;
        }
    }
    if (modern_controls) touch_sample(input);
    while (event_next < event_count && events[event_next].frame <= frame) {
        key_event *k = &events[event_next++];
        if (k->down < 0) {
            if (raw_scan(input, k->scan)) return -1;
        } else if (emit_key(input, k->scan, k->down)) return -1;
    }
    return 0;
}

unsigned platform_touch_rects(int width, int height,
                               cm_rect rectangles[CM_TOUCH_RECTS_MAX])
{
    static const uint8_t letters[6][7] = {
        {31,16,16,30,16,16,16}, {7,2,2,2,18,18,12},
        {17,17,17,17,17,17,14}, {17,17,17,21,21,21,10},
        {17,27,21,21,17,17,17}, {14,17,17,31,17,17,17}
    };
    SDL_Rect stick, buttons[6], r;
    unsigned i, x, y, count = 0;
    int unit;
    if (is_headless || !modern_controls || !gameplay_controls || !touch_visible ||
        width <= 0 || height <= 0) return 0;
    touch_layout(width, height, &stick, buttons);
    unit = height / 480 + 1;
#define RECTANGLE() do { \
    cm_rect *out = &rectangles[count++]; \
    out->x = r.x; out->y = r.y; out->w = r.w; out->h = r.h; \
} while (0)
    /* At most 7*4 outline + 6*7*5 glyph + 1 centre = 239 rectangles. */
    for (i = 0; i < 7; ++i) {
        SDL_Rect box = i ? buttons[i - 1] : stick;
        r = box; r.h = unit;
        RECTANGLE();
        r.y = box.y + box.h - unit;
        RECTANGLE();
        r = box; r.w = unit;
        RECTANGLE();
        r.x = box.x + box.w - unit;
        RECTANGLE();
        if (!i) continue;
        for (y = 0; y < 7; ++y) for (x = 0; x < 5; ++x)
            if (letters[i - 1][y] & (16u >> x)) {
                r.x = box.x + box.w / 2 + ((int)x - 2) * unit;
                r.y = box.y + box.h / 2 + ((int)y - 3) * unit;
                r.w = r.h = unit;
                RECTANGLE();
            }
    }
    r.x = stick.x + stick.w / 2 - unit * 3;
    r.y = stick.y + stick.h / 2 - unit * 3;
    r.w = r.h = unit * 6;
    RECTANGLE();
#undef RECTANGLE
    return count;
}

static int touch_overlay(SDL_Surface *dest)
{
    cm_rect rectangles[CM_TOUCH_RECTS_MAX];
    unsigned count = platform_touch_rects(dest->w, dest->h, rectangles), i;
    Uint32 color = SDL_MapRGB(dest->format, 160, 220, 220);
    for (i = 0; i < count; ++i) {
        SDL_Rect r = {rectangles[i].x, rectangles[i].y, rectangles[i].w, rectangles[i].h};
        if (SDL_FillRect(dest, &r, color)) return -1;
    }
    return 0;
}

int platform_present(const uint8_t *pixels, const uint8_t *palette)
{
    SDL_Color colors[256];
    SDL_Surface *dest;
    SDL_Rect rect;
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
    if (!dest) return cm_fail("SDL", 0, SDL_GetError());
    if (!converted || converted->format->format != dest->format->format) {
        if (converted) SDL_FreeSurface(converted);
        converted = SDL_CreateRGBSurfaceWithFormat(0, CM_WIDTH, CM_HEIGHT,
                     dest->format->BitsPerPixel, dest->format->format);
        if (!converted) return cm_fail("SDL", 0, SDL_GetError());
    }
    rect = viewport(dest->w, dest->h);
    if (SDL_BlitSurface(surface, NULL, converted, NULL) ||
        SDL_FillRect(dest, NULL, SDL_MapRGB(dest->format, 0, 0, 0)) ||
        SDL_BlitScaled(converted, NULL, dest, &rect) ||
        touch_overlay(dest) ||
        SDL_UpdateWindowSurface(window))
        return cm_fail("SDL", 0, SDL_GetError());
    return 0;
}

int platform_directory(const char *path)
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

int platform_file_write(const char *path, const uint8_t *data, uint32_t size)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "wb");
    if (!rw) return cm_fail(path, 0, SDL_GetError());
    if (SDL_RWwrite(rw, data, 1, size) != size) {
        SDL_RWclose(rw);
        return cm_fail(path, 0, "short file write");
    }
    if (SDL_RWclose(rw)) return cm_fail(path, 0, "file close failed");
    return 0;
}

int platform_state_dump(const char *directory, unsigned frame,
                        const char *text, size_t length)
{
    char path[PATH_MAX_BYTES];
    int n;
    if (length > UINT32_MAX)
        return cm_fail(directory, 0, "state dump too large");
    if (platform_directory(directory)) return -1;
    n = snprintf(path, sizeof path, "%s/frame%06u.state", directory, frame);
    if (n < 0 || (size_t)n >= sizeof path)
        return cm_fail(directory, 0, "state path too long");
    return platform_file_write(path, (const uint8_t *)text, (uint32_t)length);
}

int platform_dump(const char *directory, unsigned frame,
                  const uint8_t *pixels, const uint8_t *palette)
{
    char path[PATH_MAX_BYTES];
    static uint8_t ppm[CM_WIDTH * CM_HEIGHT * 3 + 32];
    const char header[] = "P6\n640 480\n255\n";
    unsigned i;
    int n;
    if (platform_directory(directory)) return -1;
    n = snprintf(path, sizeof path, "%s/frame%06u.idx", directory, frame);
    if (n < 0 || (size_t)n >= sizeof path)
        return cm_fail(directory, 0, "frame path too long");
    if (platform_file_write(path, pixels, CM_WIDTH * CM_HEIGHT)) return -1;
    n = snprintf(path, sizeof path, "%s/frame%06u.pal", directory, frame);
    if (n < 0 || (size_t)n >= sizeof path)
        return cm_fail(directory, 0, "frame path too long");
    if (platform_file_write(path, palette, 768)) return -1;
    memcpy(ppm, header, sizeof header - 1);
    for (i = 0; i < CM_WIDTH * CM_HEIGHT; ++i)
        memcpy(ppm + sizeof header - 1 + i * 3, palette + pixels[i] * 3, 3);
    n = snprintf(path, sizeof path, "%s/frame%06u.ppm", directory, frame);
    if (n < 0 || (size_t)n >= sizeof path)
        return cm_fail(directory, 0, "frame path too long");
    return platform_file_write(path, ppm, sizeof header - 1 + CM_WIDTH * CM_HEIGHT * 3);
}

uint32_t platform_ticks(unsigned frame)
{
    return is_headless ? frame : (uint32_t)((SDL_GetTicks64() - start) * CM_TICK_HZ / 1000);
}

void platform_wait(unsigned frame)
{
    uint64_t now = SDL_GetTicks64();
    uint64_t target = start + (uint64_t)(frame + 1) * 1000 / CM_TICK_HZ;
    if (!is_headless && now < target) SDL_Delay((Uint32)(target - now));
}

void platform_gl_close(void)
{
    if (gl_context) SDL_GL_DeleteContext(gl_context);
    gl_context=NULL;
    if (gl_window && window) SDL_DestroyWindow(window);
    if (gl_window) window=NULL;
    gl_window=0;
}

int platform_gl_open(int headless,unsigned width,unsigned height,int *gles)
{
    Uint32 flags=SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI;
    int es=0,major=0,minor=0;
    if (!width || !height || width>8192 || height>8192 || gl_context)
        return cm_fail("SDL GL",0,"invalid GL context request");
    if (!(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO)) {
        if (headless) SDL_SetHintWithPriority("SDL_VIDEODRIVER","offscreen",SDL_HINT_DEFAULT);
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) return cm_fail("SDL GL video",0,SDL_GetError());
    }
    if (surface) SDL_FreeSurface(surface);
    if (converted) SDL_FreeSurface(converted);
    surface=converted=NULL;
    if (window) SDL_DestroyWindow(window);
    window=NULL;
#ifdef __ANDROID__
    es=1;
    flags|=SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
    flags|=headless ? SDL_WINDOW_HIDDEN:SDL_WINDOW_RESIZABLE;
#endif
    SDL_GL_ResetAttributes();
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,es ? SDL_GL_CONTEXT_PROFILE_ES:SDL_GL_CONTEXT_PROFILE_CORE) ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3) ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,es ? 0:3) ||
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1) ||
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24))
        return cm_fail("SDL GL attributes",0,SDL_GetError());
    window=SDL_CreateWindow("CyberMage Enhanced",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                            (int)width,(int)height,flags);
    if (!window) return cm_fail("SDL GL window",0,SDL_GetError());
    gl_window=1;gl_context=SDL_GL_CreateContext(window);
    if (!gl_context || SDL_GL_MakeCurrent(window,gl_context)) {
        cm_fail("SDL GL context",0,SDL_GetError());platform_gl_close();return -1;
    }
    if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,&major) ||
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,&minor) ||
        major<3 || (!es && major==3 && minor<3)) {
        cm_fail("SDL GL version",0,"requires OpenGL3.3 core or ES3.0");platform_gl_close();return -1;
    }
    (void)SDL_GL_SetSwapInterval(headless ? 0:1);
    *gles=es;
    return 0;
}

void *platform_gl_proc(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

void platform_gl_size(unsigned *width,unsigned *height)
{
    int w=0,h=0;
    if (gl_context && window) SDL_GL_GetDrawableSize(window,&w,&h);
    *width=w>0 ? (unsigned)w:0;*height=h>0 ? (unsigned)h:0;
}

int platform_gl_swap(void)
{
    if (!gl_context || !window) return cm_fail("SDL GL",0,"GL context not open");
    SDL_GL_SwapWindow(window);return 0;
}
