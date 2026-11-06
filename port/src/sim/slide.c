#include "sim_internal.h"

int32_t sim_mul16(int32_t a, int32_t b)
{
    int64_t p = (int64_t)a * b;
    /* SHRD floors negative products, unlike signed /65536. */
    return (int32_t)(uint32_t)((uint64_t)p >> 16);
}

/* Original B427C is a quarter-turn tangent table with eight-angle-unit
 * steps. Mirroring uses 2047-index, not 2048-index. */
int32_t sim_slope(const world *w, uint16_t angle)
{
    unsigned quadrant = angle / SIM_ANGLE_QUARTER;
    unsigned index = (angle % SIM_ANGLE_QUARTER) / 8;
    int32_t slope;
    if (quadrant & 1) index = CM_TRIG_SIZE - 1 - index;
    slope = w->trig.tan[index];
    return quadrant >= 2 ? -slope : slope;
}

static int32_t axis_delta(int32_t old, int32_t low, int32_t high)
{
    if (old < low) return low - 1 - old;
    if (old > high) return high + 1 - old;
    return 0;
}

static int prefer_y(uint16_t angle)
{
    return !(((uint16_t)(angle + SIM_ANGLE_EIGHTH) / SIM_ANGLE_QUARTER) & 1);
}

/* CM_HI.EXE Xox::NewNxNy 044FC8. Candidate correction is measured from
 * original centre, never accumulated from the already clipped proposal. */
void sim_new_nx_ny(sim_state *s, const world *w, unsigned object,
                   int32_t *x, int32_t *y, int qx, int qy)
{
    const world_object *o = &w->objects[object];
    uint16_t angle = cm_atan64k(o->x, o->y, *x, *y);
    int32_t low_x = (int32_t)((uint32_t)*x & 0xffff0000u) + qx * SIM_QUARTER_TILE;
    int32_t low_y = (int32_t)((uint32_t)*y & 0xffff0000u) + qy * SIM_QUARTER_TILE;
    int32_t dx = axis_delta(o->x, low_x, low_x + SIM_QUARTER_TILE);
    int32_t dy = axis_delta(o->y, low_y, low_y + SIM_QUARTER_TILE);
    int32_t cx = o->x, cy = o->y, cross;
    cross = o->y + sim_mul16(dx < 0 ? -dx : dx, sim_slope(w, angle));
    if (cross >= low_y && cross < low_y + SIM_QUARTER_TILE) cx += dx;
    cross = o->x + sim_mul16(dy < 0 ? -dy : dy,
                             -sim_slope(w, (uint16_t)(angle + SIM_ANGLE_QUARTER)));
    if (cross >= low_x && cross < low_x + SIM_QUARTER_TILE) cy += dy;
    if (prefer_y(angle)) {
        if (!sim_path_check(s, w, *x, cy)) { *y = cy; return; }
        if (!sim_path_check(s, w, cx, *y)) { *x = cx; return; }
    } else {
        if (!sim_path_check(s, w, cx, *y)) { *x = cx; return; }
        if (!sim_path_check(s, w, *x, cy)) { *y = cy; return; }
    }
    *x = cx;
    *y = cy;
}

/* Xox::ObjCheck 044A88 uses strict rectangle overlap, not distance. */
static int obj_check(const world_object *o, int32_t x, int32_t y)
{
    int32_t radius = o->unk_74AF0;
    return (int64_t)x + SIM_OBJECT_MARGIN > (int64_t)o->x - radius &&
           (int64_t)x - SIM_OBJECT_MARGIN < (int64_t)o->x + radius &&
           (int64_t)y + SIM_OBJECT_MARGIN > (int64_t)o->y - radius &&
           (int64_t)y - SIM_OBJECT_MARGIN < (int64_t)o->y + radius;
}

/* Xox::ObjNxNy 044B00. The 1E00 relaxation is applied only after a
 * trajectory/expanded-other-axis test; it is not added to the path mask. */
void sim_obj_nx_ny(sim_state *s, const world *w, unsigned object, unsigned other,
                   int32_t *x, int32_t *y)
{
    const world_object *o = &w->objects[object], *b = &w->objects[other];
    uint16_t angle = cm_atan64k(o->x, o->y, *x, *y);
    int32_t radius = b->unk_74AF0;
    int32_t low_x = b->x - radius, low_y = b->y - radius;
    int32_t high_x = b->x + radius, high_y = b->y + radius;
    int32_t dx = axis_delta(o->x, low_x, high_x);
    int32_t dy = axis_delta(o->y, low_y, high_y);
    int32_t cx = o->x, cy = o->y, cross;
    int32_t target_delta = (int16_t)(cm_atan64k(o->x, o->y, b->x, b->y) - angle);
    if (target_delta < 0) target_delta = -target_delta;
    if (target_delta < 0x3000) s->collision_flags |= 2;
    cross = o->y + sim_mul16(dx < 0 ? -dx : dx, sim_slope(w, angle));
    if (cross >= low_y - radius && cross < high_y + radius)
        cx += dx + (dx < 0 ? SIM_OBJECT_MARGIN : -SIM_OBJECT_MARGIN);
    cross = o->x + sim_mul16(dy < 0 ? -dy : dy,
                             -sim_slope(w, (uint16_t)(angle - SIM_ANGLE_QUARTER)));
    if (cross >= low_x - radius && cross < high_x + radius)
        cy += dy + (dy < 0 ? SIM_OBJECT_MARGIN : -SIM_OBJECT_MARGIN);
    if (prefer_y(angle)) {
        if (!obj_check(b, *x, cy) && !sim_path_check(s, w, *x, cy)) { *y = cy; return; }
        if (!obj_check(b, cx, *y) && !sim_path_check(s, w, cx, *y)) { *x = cx; return; }
    } else {
        if (!obj_check(b, cx, *y) && !sim_path_check(s, w, cx, *y)) { *x = cx; return; }
        if (!obj_check(b, *x, cy) && !sim_path_check(s, w, *x, cy)) { *y = cy; return; }
    }
    if (sim_path_check(s, w, cx, cy)) {
        *x = o->x;
        *y = o->y;
    } else {
        *x = cx;
        *y = cy;
    }
}
