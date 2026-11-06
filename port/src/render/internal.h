#ifndef CM_RENDER_INTERNAL_H
#define CM_RENDER_INTERNAL_H
#include "render.h"
#include <limits.h>

#define R_FIXED_ONE INT32_C(65536)
#define R_FIXED_HALF INT32_C(32768)
#define R_NEAR 1024
#define R_RAY_MAX 18
#define R_MAX_COORD INT32_C(2080768)
#define R_WALL UINT32_C(0x04000000)
#define R_FXO UINT32_C(0x08000000)
#define R_PROP UINT32_C(0x20000000)
#define R_FLAT UINT32_C(0x02000000)
#define R_TRANSPARENT UINT32_C(0x40000000)
#define R_PACKED UINT32_C(0x00080000)
#define R_TOP UINT32_C(0x00800000)
#define R_FLOOR UINT32_C(0x00400000)
#define R_MIRROR UINT32_C(0x80000000)
enum { R_COLUMN_SOLID, R_COLUMN_MASK, R_COLUMN_PACK, R_COLUMN_INVIS, R_COLUMN_AURA };
enum { R_SHADE_MAPS=64, R_FIXED_MAPS=10, R_FOG_MAPS=21, R_COLORS=256 };
enum { R_FLAT_SIDE=128, R_FLAT_BYTES=R_FLAT_SIDE*R_FLAT_SIDE };

static inline int32_t r_bits(uint32_t v) { return v<=INT32_MAX ? (int32_t)v:-1-(int32_t)(UINT32_MAX-v); }
static inline int32_t r_add(int32_t a,int32_t b) { return r_bits((uint32_t)a+(uint32_t)b); }
static inline int32_t r_sub(int32_t a,int32_t b) { return r_bits((uint32_t)a-(uint32_t)b); }
static inline int32_t r_mul(int32_t a,int32_t b) { return r_bits((uint32_t)a*(uint32_t)b); }
static inline int32_t r_shl(int32_t a,unsigned b) { return r_bits((uint32_t)a << b); }
static inline int32_t r_mul16(int32_t a,int32_t b) { return r_bits((uint32_t)((uint64_t)((int64_t)a*b) >> 16)); }
static inline int32_t r_abs(int32_t a) { return a < 0 ? r_sub(0,a) : a; }
static inline int r_min(int a,int b) { return a < b ? a : b; }
static inline int r_max(int a,int b) { return a > b ? a : b; }
static inline int r_clamp(int a,int lo,int hi) { return r_min(r_max(a,lo),hi); }
static inline uint16_t r_u16(const uint8_t *p) { return (uint16_t)(p[0] | (unsigned)p[1]<<8); }
static inline uint32_t r_u32(const uint8_t *p) { return (uint32_t)r_u16(p) | (uint32_t)r_u16(p+2)<<16; }
static inline const world_object *r_object(const world *w,const render_state *s,unsigned id) {
    return id < CM_RENDER_DYNAMIC_FIRST ? &w->objects[id] : &s->generated[id-CM_RENDER_DYNAMIC_FIRST];
}
static inline int r_fail(render_state *s,const char *message,unsigned offset) {
    s->error = -1; return cm_fail("render_sw",offset,message);
}
int r_prepare(const world *w,const world_camera *c,render_state *s);
int r_sync_textures(const world *w,render_state *s);
int r_build_lists(const world *w,render_state *s);
int r_before(const world *w,render_state *s,unsigned a,unsigned b);
int r_scene(const world *w,render_state *s,render_visibility_list *out);
int r_paint(const world *w,render_state *s);
int r_project(const world *w,const render_state *s,int32_t x,int32_t y,int32_t h,int32_t *sx,int32_t *sy,int32_t near);
int r_new_point(const world *w,const render_state *s,int32_t x,int32_t y,uint16_t angle,int32_t h,int32_t *sx,int32_t *sy);
int r_find_point(const world *w,const render_state *s,int32_t xa,int32_t ya,int32_t xb,int32_t h,int32_t *sx,int32_t *sy);
int r_connect(const world *w,const render_state *s,render_quad *q,const int32_t wx[4],const int32_t wy[4],int32_t h,render_edge *l,render_edge *r,int32_t *top,int32_t *x0);
int r_draw_wall(const world *w,render_state *s,const world_object *o,const render_quad *q,unsigned light);
int r_draw_prop(const world *w,render_state *s,const world_object *o,const render_quad *q,unsigned texture,unsigned light);
int r_draw_flat(const world *w,render_state *s,const world_object *o,render_quad *q,int32_t h,int top,unsigned light);
const uint8_t *r_colormap(const world *w,render_state *s,unsigned light,int32_t distance,int *fixed);
int r_column(render_state *s,const render_texture *tex,uint32_t base,int x,int y,int count,uint32_t step,uint32_t pos,const uint8_t *map,int mode);
int32_t r_tan(const world *w,uint16_t angle);
int32_t r_yslope(int32_t x1,int32_t y1,int32_t x2,int32_t y2);
int32_t r_xslope(int32_t x1,int32_t y1,int32_t x2,int32_t y2);
#endif
