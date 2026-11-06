#ifndef CM_TARGET_H
#define CM_TARGET_H
#include "vis.h"

#define CM_TARGET_COLUMN 320

typedef struct {
    unsigned count;
    uint16_t objects[CM_OBJECT_MAX];
} target_list;

/* Original framebuffer coordinates, independent of presentation backend.
 * Camera/world/extents are borrowed; query changes only output and scratch. */
int target_query(target_list *out, const world *w, const world_camera *camera,
                 const vis_view *view);
#endif
