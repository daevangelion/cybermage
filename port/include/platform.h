#ifndef CM_PLATFORM_H
#define CM_PLATFORM_H

#include "cm.h"

#define CM_FILE_MAX (128u * 1024u * 1024u)
#define CM_KEY_COUNT 128
#define CM_RAW_SCANS_MAX 256
#define CM_TOUCH_MAX 32

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
    int mouse_x, mouse_y;
    int mouse_dx, mouse_dy;
    unsigned mouse_buttons;
    unsigned mouse_pressed, mouse_released;
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
int platform_present(const uint8_t *pixels, const uint8_t *palette);
int platform_dump(const char *directory, unsigned frame,
                  const uint8_t *pixels, const uint8_t *palette);
int platform_state_dump(const char *directory, unsigned frame,
                        const char *text, size_t length);
void platform_wait(unsigned frame);
uint32_t platform_ticks(unsigned frame);
void platform_audio_lock(void);
void platform_audio_unlock(void);
int platform_report(const char *message);

#endif
