#ifndef CM_VIS_INTERNAL_H
#define CM_VIS_INTERNAL_H
#include "vis.h"
#include <limits.h>

#define VIS_FIXED_ONE INT32_C(65536)
#define VIS_FIXED_HALF INT32_C(32768)
#define VIS_NEAR 1024
#define VIS_RAY_MAX 18
#define VIS_MAX_COORD INT32_C(2080768)
#define VIS_DYNAMIC_FIRST CM_VIS_DYNAMIC_FIRST
#define VIS_NODES CM_VIS_NODES
#define VIS_CELLS CM_VIS_CELLS
#define VIS_WALL UINT32_C(0x04000000)
#define VIS_FXO UINT32_C(0x08000000)
#define VIS_PROP UINT32_C(0x20000000)
#define VIS_FLAT UINT32_C(0x02000000)
#define VIS_TRANSPARENT UINT32_C(0x40000000)
#define VIS_PACKED UINT32_C(0x00080000)

static inline int32_t vis_bits(uint32_t v) { return v<=INT32_MAX ? (int32_t)v:-1-(int32_t)(UINT32_MAX-v); }
static inline int32_t vis_add(int32_t a,int32_t b) { return vis_bits((uint32_t)a+(uint32_t)b); }
static inline int32_t vis_sub(int32_t a,int32_t b) { return vis_bits((uint32_t)a-(uint32_t)b); }
static inline int32_t vis_mul(int32_t a,int32_t b) { return vis_bits((uint32_t)a*(uint32_t)b); }
static inline int32_t vis_shl(int32_t a,unsigned b) { return vis_bits((uint32_t)a << b); }
static inline int32_t vis_mul16(int32_t a,int32_t b) { return vis_bits((uint32_t)((uint64_t)((int64_t)a*b) >> 16)); }
static inline int32_t vis_abs(int32_t a) { return a < 0 ? vis_sub(0,a) : a; }
static inline int vis_min(int a,int b) { return a < b ? a : b; }
static inline int vis_max(int a,int b) { return a > b ? a : b; }
static inline int vis_clamp(int a,int lo,int hi) { return vis_min(vis_max(a,lo),hi); }
static inline const world_object *vis_object(const world *w,const vis_state *s,unsigned id) {
    return id < VIS_DYNAMIC_FIRST ? &w->objects[id] : &s->generated[id-VIS_DYNAMIC_FIRST];
}
static inline int vis_fail(vis_state *s,const char *message,unsigned offset) {
    s->error = -1; return cm_fail("visibility",offset,message);
}
int vis_before(const world *w,vis_state *s,unsigned a,unsigned b);
#endif
