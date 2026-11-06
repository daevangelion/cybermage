#ifndef CM_SIM_INTERNAL_H
#define CM_SIM_INTERNAL_H
#include "sim.h"

enum {
    SIM_TILE_UNIT = 65536,
    SIM_QUARTER_TILE = 0x4000,
    SIM_HEAD_CLEARANCE = 0x4800,
    SIM_TOP_CLEARANCE = 0x1100,
    SIM_BODY_MIN = 0x5800,
    SIM_OBJECT_MARGIN = 0x1e00,
    SIM_SPEED_MAX = 256,
    SIM_YAW_MAX = 128,
    SIM_ANGLE_QUARTER = 0x4000,
    SIM_ANGLE_HALF = 0x8000,
    SIM_ANGLE_EIGHTH = 0x2000
};

int32_t sim_mul16(int32_t a, int32_t b);
int32_t sim_slope(const world *w, uint16_t angle);
int sim_new_pc_bound(sim_state *s, world *w, unsigned object, int32_t *x, int32_t *y);
int sim_monster_bound(sim_state *s, world *w, unsigned object, int32_t *x, int32_t *y);
void sim_new_nx_ny(sim_state *s, const world *w, unsigned object,
                   int32_t *x, int32_t *y, int qx, int qy);
void sim_obj_nx_ny(sim_state *s, const world *w, unsigned object, unsigned other,
                   int32_t *x, int32_t *y);
int sim_fix_nx_ny(sim_state *s, const world *w, unsigned object, unsigned other,
                  int32_t *x, int32_t *y);
int sim_coll_object(sim_state *s, world *w, unsigned object, int32_t *x, int32_t *y);
#endif
