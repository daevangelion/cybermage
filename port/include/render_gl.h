#ifndef CM_RENDER_GL_H
#define CM_RENDER_GL_H

#include "gfx.h"
#include "render.h"

/* Enhanced presentation only. Visibility/targeting always use render.h. */
int render_gl_open(int headless,unsigned width,unsigned height);
int render_gl_load(const world *w,const render_state *assets,
                    const uint8_t palette[768]);
int render_gl_resize(unsigned width,unsigned height);
int render_gl_draw(const world *w,const world_camera *camera);
int render_gl_present(const cm_overlay *overlay);
/* Indexed framebuffer, bottom row first (native GL orientation). */
int render_gl_readback(uint8_t *pixels,size_t bytes,unsigned stride);
void render_gl_dimensions(unsigned *width,unsigned *height);
const char *render_gl_driver(void);
void render_gl_close(void);

#endif
