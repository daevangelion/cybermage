#ifndef CM_GFX_H
#define CM_GFX_H

#include "cm.h"

#define CM_OVERLAY_MAPS 254
typedef struct {
    uint8_t *pixels;
    uint8_t coverage[CM_WIDTH * CM_HEIGHT]; /* 0 keep,1 replace,2+ LUT row */
    uint32_t source[CM_WIDTH * CM_HEIGHT]; /* LUT input, canvas coordinates */
    uint8_t maps[CM_OVERLAY_MAPS][256];
    const uint8_t *map_source[CM_OVERLAY_MAPS];
    uint16_t previous[CM_OVERLAY_MAPS];
    unsigned map_count;
    uint16_t world_map; /* Explicit world tint beyond the4:3 HUD canvas. */
    const uint8_t *last_source;
    uint16_t last_previous, last_row;
    int error;
} cm_overlay;

int gfx_overlay_begin(cm_overlay *overlay,uint8_t *pixels);
int gfx_overlay_end(void);
void gfx_store(uint8_t *dst,uint8_t color);
void gfx_map_pixel(uint8_t *dst,const uint8_t table[256]);
void gfx_cover(uint8_t *dst,unsigned count);
void gfx_copy(uint8_t *dst,const uint8_t *src,unsigned count);
void gfx_world_map(const uint8_t table[256]);

typedef struct {
    const uint8_t *data;
    uint16_t height;
    uint16_t stride;
    uint16_t count;
    int spacing;
    const char *path;
} cm_font;

int gfx_palette(uint8_t rgb[768], const uint8_t *data, uint32_t size,
                const char *path);
int gfx_font(cm_font *f, const uint8_t *data, uint32_t size, const char *path);
int gfx_text_width(const cm_font *f, const char *text, int *width);
int gfx_text(uint8_t *dst, const cm_font *f, int x, int y, const char *text);
int gfx_menu_text(uint8_t *dst, const cm_font *f, int x, int y,
                  const char *text);
void gfx_blit(uint8_t *dst, int x, int y, const uint8_t *src,
              unsigned width, unsigned height);
void gfx_shade(uint8_t *dst, int x0, int y0, int x1, int y1,
               const uint8_t table[256]);
/* Same CPU rectangle; GPU capture also tints extended world-dependent pixels. */
void gfx_shade_world(uint8_t *dst,int x0,int y0,int x1,int y1,
                      const uint8_t table[256]);

#endif
