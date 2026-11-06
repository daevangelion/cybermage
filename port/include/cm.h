#ifndef CM_H
#define CM_H

#include <stddef.h>
#include <stdint.h>

#define CM_WIDTH 640
#define CM_HEIGHT 480
#define CM_TICK_HZ 40

typedef struct {
    const char *file;
    uint32_t offset;
    const char *message;
} cm_error;

/* Timer input at an original draw instruction, not a frame-entry estimate.
 * read returns zero or a diagnosed -1. The deadline read is conditional. */
typedef struct {
    void *context;
    int (*read)(void *context, uint32_t site, uint32_t *ticks);
} cm_draw_clock;

/* Platform owns diagnostics; failures return -1, success returns zero. */
int cm_fail(const char *file, uint32_t offset, const char *message);
const cm_error *cm_last_error(void);

static inline uint16_t cm_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static inline uint32_t cm_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#endif
