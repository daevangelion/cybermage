#ifndef CM_RENDER_H
#define CM_RENDER_H

#include "vis.h"

#define CM_RENDER_WIDTH 640
#define CM_RENDER_HEIGHT 480
#define CM_RENDER_TEXTURES CM_PANEL_MAX

typedef struct {
    uint16_t object;
    int32_t left, right;
    uint8_t targetable;
} render_visible_prop;
typedef struct {
    unsigned count;
    render_visible_prop props[CM_OBJECT_MAX];
} render_visibility_list;
typedef struct {
    const uint8_t *data;
    uint32_t size;
    int32_t width, height; /* Half extents; column-major, as TEX_W/TEX_H. */
    int32_t frame_half_w, frame_base, frame_depth, frame_height;
    const uint8_t *source;
    uint32_t source_size,revision,decoded_offset,decoded_capacity;
} render_texture;
typedef struct { int32_t x[4], y[4]; uint8_t visible[4]; } render_quad;
enum { RENDER_WALL, RENDER_FLAT, RENDER_PROP };
typedef struct {
    uint16_t object, texture;
    uint8_t kind, light, top;
    int32_t height, horizon;
    render_quad quad;
} render_command;
typedef struct { int32_t x[3], end[3], slope[4], last; } render_edge;
typedef struct {
    int x, y, width, height, focal, horizon;
    world_camera camera;
    int32_t camera_cos, camera_sin;
    int32_t u_cos, u_sin, u_step, v_cos, v_sin, v_step;
    uint8_t *framebuffer;
    const world *visible_world;
    render_texture textures[CM_RENDER_TEXTURES];
    unsigned textures_loaded, decoded_bytes;
    const uint8_t *shade_maps, *fixed_maps, *fog_maps;
    world_span backdrop;
    uint8_t sky_fill, sky_fog, fog_on, parity;
    int32_t curve, shade_bias, shade_shift, shade_scale;
    uint32_t shade_override_revision;
    vis_state scene;
    unsigned command_count;
    render_command commands[CM_OBJECT_MAX];
    int error;
} render_state;

/* State/assets are retained, no allocation or world mutation during a frame. */
int render_load(const world *w, rsc *hipak);
int render_sync_textures(const world *w);
int render_prop_texture(const world_camera *camera,const world_object *object);
void render_unload(void);
int render_set_view(int x, int y, int width, int height);
render_state *render_get_state(void);
int render_visibility(const world *w, const world_camera *c,
                      render_visibility_list *out);
void render_view(const world *w, const world_camera *c, uint8_t *fb);
int render_draw(const world *w, const world_camera *c, uint8_t *fb);
/* Paint the prepared visibility lists; world must not change between passes. */
int render_paint(const world *w, uint8_t *fb);
int render_shade_distance(const render_state *s, int32_t distance);
int render_fog_distance(int32_t distance);

#endif
