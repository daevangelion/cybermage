#ifndef CM_PLATFORM_H
#define CM_PLATFORM_H

#include "cm.h"

#define CM_FILE_MAX (128u * 1024u * 1024u)
#define CM_KEY_COUNT 128
#define CM_RAW_SCANS_MAX 256
#define CM_TOUCH_MAX 32
#define CM_TOUCH_RECTS_MAX 256
#define CM_GL_SIZE_MAX 8192u

typedef struct { int x, y, w, h; } cm_rect;

enum { CM_TOUCH_DOWN = 1, CM_TOUCH_MOVE, CM_TOUCH_UP };

typedef struct {
    int64_t id;
    int x, y, dx, dy; /* 640x480 game coordinates; may lie in letterbox */
    uint16_t pressure;
    uint8_t phase, inside;
} cm_touch;

typedef struct {
    uint8_t raw_scans[CM_RAW_SCANS_MAX]; /* ordered IRQ9 bytes, including E0 */
    unsigned raw_count;
    uint8_t held[CM_KEY_COUNT]; /* DOS set-1 scan codes */
    uint8_t pressed[CM_KEY_COUNT];
    uint8_t released[CM_KEY_COUNT];
    int mouse_present, joystick_present; /* Driver/device availability, not coordinates. */
    int mouse_x, mouse_y;
    int mouse_dx, mouse_dy;
    int16_t mouse_mickey_x, mouse_mickey_y; /* DOS AX0B signed16 counts; enhanced deltas stay above. */
    uint16_t mouse_press_count[2]; /* DOS left/right press counters, consumed per poll. */
    unsigned mouse_buttons;
    unsigned mouse_pressed, mouse_released;
    int mouse_wheel;
    int16_t pad_axes[6]; /* SDL GameController left/right XY, LT, RT. */
    uint32_t pad_buttons;
    uint32_t pad_pressed, pad_released;
    int focus_lost, pad_disconnected;
    int render_reset;
    int16_t touch_move_x, touch_move_y;
    int touch_look_dx, touch_look_dy;
    uint32_t touch_buttons; /* CM_CONTROL_* bit positions. */
    uint32_t touch_pressed;
    int16_t remote_turn; /* TV remote left/right as a signed turn axis. */
    int modern;
    int32_t move_speed, look_yaw, look_pitch;
    uint16_t move_angle;
    uint8_t jump, aim, run;
    int8_t fly, cycle;
    uint8_t select_kind, select_slot, select_open;
    cm_touch touches[CM_TOUCH_MAX];
    unsigned touch_count;
    int quit;
} cm_input;

typedef struct {
    uint8_t *data;
    uint32_t size;
} cm_file;

int platform_file_read(cm_file *f, const char *path);
int platform_file_write(const char *path, const uint8_t *data, uint32_t size);
int platform_file_exists(const char *path);
void platform_file_release(cm_file *f);
int platform_init(int headless);
const char *platform_user_dir(void);
int platform_directory(const char *path);
void platform_shutdown(void);
int platform_keys(const char *path);
int platform_poll(cm_input *input, unsigned frame);
void platform_mouse_warp(int x, int y); /* 640x480 game coordinates. */
int platform_controls(int modern, int gameplay);
/* TV remote profile (modern gameplay only): left/right turn, centre tap uses,
   centre hold fires, centre double-tap jumps, Back tap menu, Back hold next. */
void platform_remote(int on);
/* Software output: 0 keeps a centred 4:3 canvas, 1 stretches it to the window. */
void platform_display_fill(int fill);
/* RGB 160,220,220; window/output coordinates, top-left origin. */
unsigned platform_touch_rects(int width, int height,
                               cm_rect rectangles[CM_TOUCH_RECTS_MAX]);
int platform_present(const uint8_t *pixels, const uint8_t *palette);
int platform_dump(const char *directory, unsigned frame,
                  const uint8_t *pixels, const uint8_t *palette);
int platform_state_dump(const char *directory, unsigned frame,
                        const char *text, size_t length);
void platform_wait(unsigned frame);
uint32_t platform_ticks(unsigned frame);
int platform_tick_step(unsigned ticks); /* Headless cadence; init resets to1. */
void platform_audio_lock(void);
void platform_audio_unlock(void);
int platform_report(const char *message);
int platform_gl_open(int headless,unsigned width,unsigned height,int *gles);
void *platform_gl_proc(const char *name);
void platform_gl_size(unsigned *width,unsigned *height);
int platform_gl_swap(void);
void platform_gl_close(void);
typedef int (*cm_gl_readback)(uint8_t *pixels,size_t bytes);
int platform_gl_dump(const char *directory,unsigned frame,unsigned width,
                      unsigned height,cm_gl_readback read_rgba);

#endif
