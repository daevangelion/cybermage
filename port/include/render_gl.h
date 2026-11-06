#ifndef CM_RENDER_GL_H
#define CM_RENDER_GL_H

#include "gfx.h"
#include "render.h"

typedef struct {
    int x,y;
    unsigned width,height;
    unsigned reference_width,reference_height; /* Original width/2 focal. */
    int preserve_outside; /* Secondary views retain the primary image. */
} cm_gl_view;

/* Enhanced presentation only. Visibility/targeting use the shared vis core. */
int render_gl_open(int headless,unsigned width,unsigned height);
int render_gl_reset(void); /* Rebuild after platform reports a lost context. */
int render_gl_palette(const uint8_t palette[768]);
int render_gl_load(const world *w,const render_state *assets,
                    const uint8_t palette[768]);
int render_gl_resize(unsigned width,unsigned height);
int render_gl_clear(uint8_t index);
int render_gl_draw(const world *w,const world_camera *camera);
int render_gl_draw_view(const world *w,const world_camera *camera,const cm_gl_view *view);
/* Composite to the GL backbuffer; caller swaps through platform_gl_swap. */
int render_gl_present(const cm_overlay *overlay);
/* Indexed framebuffer, bottom row first (native GL orientation). */
int render_gl_readback(uint8_t *pixels,size_t bytes,unsigned stride);
int render_gl_read_rgba(uint8_t *pixels,size_t bytes);
/* Actual GPU indices at aspect-kept HUD pixel centres, top row first.
 * The optional overlay is composed by the same shader as normal presentation. */
int render_gl_read_canvas(const cm_overlay *overlay,uint8_t pixels[CM_WIDTH*CM_HEIGHT]);
void render_gl_dimensions(unsigned *width,unsigned *height);
const char *render_gl_driver(void);
void render_gl_close(void);

#endif
