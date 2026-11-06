#include "combat_internal.h"

/* HandleNextFrame 0x1CF71..0x1D09E. Flash decay belongs to RenderBells,
 * not this block: each intervening frame consumes all three random calls.
 * +1444FC/+144500 are spills used only here; keep horizontal push local. */
int ai_pc_hit_response(ai_state *a, sim_state *s, world *w)
{
    int32_t push;
    unsigned angle, side;
    a->pc.hit_camera_z = 0;
    if (!a->pc_flash) return 0;
    if (w->player_object >= w->object_count)
        return cm_fail("HandleNextFrame hit", w->player_object, "invalid player object");
    a->pc.hit_camera_z = world_rnd(&w->rng, 4096) - 2048;
    push = world_rnd(&w->rng, 8192) - 4096;
    side = (unsigned)world_rnd(&w->rng, 2);
    angle = (uint16_t)(w->objects[w->player_object].heading +
                        (side ? 0x4000 : 0xC000)) >> 5;
    return sim_jumble_bound(s, w, w->player_object,
                            ai_fixed32(push, w->trig.cos[angle]),
                            ai_fixed32(push, w->trig.sin[angle]), 0);
}
