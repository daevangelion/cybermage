#ifndef CM_VIS_H
#define CM_VIS_H
#include "world.h"

#define CM_VIS_DYNAMIC_FIRST 2378
#define CM_VIS_DYNAMIC_MAX (CM_OBJECT_MAX - CM_VIS_DYNAMIC_FIRST)
#define CM_VIS_NODES 2842
#define CM_VIS_CELLS 512

typedef struct { int x, y, width, height; } vis_view;
typedef struct { int32_t prev, next; uint16_t object; } vis_node;
typedef struct { uint32_t info; int32_t next, prev; uint16_t cell; } vis_cell;
/* Shared pixel-free scene scratch. Renderers consume these ordered lists,
 * but cannot supply gameplay target candidates or mutate the world. */
typedef struct {
    int x, y, width, height, focal, horizon;
    world_camera camera;
    uint16_t ray_angles[640];
    int32_t camera_cos, camera_sin;
    world_object generated[CM_VIS_DYNAMIC_MAX];
    unsigned generated_count, node_count, cell_count;
    vis_node nodes[CM_VIS_NODES];
    vis_cell cells[CM_VIS_CELLS];
    int32_t cell_head, object_head[CM_MAP_LEVELS * CM_MAP_CELLS];
    int32_t object_tail[CM_MAP_LEVELS * CM_MAP_CELLS];
    int32_t step_first[38];
    uint16_t object_count[CM_OBJECT_MAX], prop_level[CM_OBJECT_MAX];
    uint8_t object_kind[CM_OBJECT_MAX];
    int32_t object_distance[CM_OBJECT_MAX];
    uint8_t distance_seen[CM_OBJECT_MAX];
    uint32_t tile_seen[CM_MAP_CELLS], tile_block[CM_MAP_CELLS];
    uint8_t side_done[CM_MAP_CELLS], object_done[CM_MAP_CELLS];
    uint8_t floor_done[CM_MAP_CELLS], ceiling_done[CM_MAP_CELLS];
    uint8_t trans_done[CM_MAP_CELLS], above_seen[CM_MAP_CELLS];
    uint8_t below_seen[CM_MAP_CELLS];
    unsigned tile, side, level, level_bit, level_shift, steps, order;
    unsigned blocked, above_open, below_open, budget, depth_above, depth_below;
    int error;
} vis_state;

int vis_prepare(const world *w, const world_camera *camera,
                   const vis_view *view, vis_state *scratch);
int vis_build_lists(const world *w, vis_state *scratch);
#endif
