#include "sim.h"
#include <string.h>

/* CM_HI.EXE: CM_ISR9 067B7B. E0 selects a different bit pair; it is not
 * a separate key table. Shifts only prime the latch (BIOS tracks modifiers). */
void sim_scan(sim_state *s, uint8_t scan)
{
    unsigned key = scan & 0x7f;
    if (scan >= 0xe0) {
        s->kb_latch = 12;
    } else if (key == 0x2a || key == 0x36) {
        unsigned bit = key == 0x2a ? 2u : 1u;
        if (scan & 0x80) s->bios_shift &= (uint8_t)~bit;
        else s->bios_shift |= (uint8_t)bit;
        s->kb_latch = 3;
    } else {
        if (scan & 0x80) s->kb_scan_status[key] &= 10;
        else s->kb_scan_status[key] = s->kb_latch;
        s->kb_latch = 3;
    }
}

void sim_init(sim_state *s, uint32_t ticks)
{
    memset(s, 0, sizeof *s);
    s->pc_mode = 5; /* Xox::InitPC CM_HI.EXE:0107A3. */
    s->replay_delta = -1;
    s->timer_ticks = s->previous_ticks = s->bp_limit_time = ticks;
    s->unk_144b00 = 16;
    /* CM_HI.EXE Xox::ReKeyDefaults 052644..052718. */
    s->kb_side_toggle = 0x2a;
    s->kb_side_left = 0x2c;
    s->kb_side_right = 0x2e;
    s->kb_jump = 0x1d;
    s->kb_crouch = 0x52;
    s->kb_look_down = 0x10;
    s->kb_look_up = 0x12;
    s->kb_look_center = 0x13;
    s->kb_side_toggle_bit = s->kb_side_left_bit = s->kb_side_right_bit = 1;
    s->kb_jump_bit = 5;
    s->kb_crouch_bit = s->kb_look_center_bit = 1;
    s->kb_look_down_bit = s->kb_look_up_bit = 1;
}

void sim_input(sim_state *s, const cm_input *input)
{
    unsigned i;
    for (i = 0; i < input->raw_count; ++i)
        sim_scan(s, input->raw_scans[i]);
}

/* Xox::HandleNextFrame 01B9EA..01BA49. One step per rendered frame;
 * discard elapsed ticks beyond two, rather than running catch-up steps. */
int32_t sim_clock(sim_state *s, uint32_t ticks)
{
    uint32_t elapsed = ticks - s->previous_ticks;
    s->timer_ticks = ticks;
    s->frame_ticks = elapsed > SIM_FRAME_TICKS_MAX ? SIM_FRAME_TICKS_MAX : (int32_t)elapsed;
    if (elapsed) s->previous_ticks = ticks;
    s->bp_limit_flag = ticks - s->bp_limit_time;
    if (s->bp_limit_flag >= 2) s->bp_limit_time = ticks;
    else s->bp_limit_flag = 0;
    return s->frame_ticks;
}

/* Xox::Turn 02AD6C; the half-turn tie follows the original >= branch. */
uint16_t sim_turn(uint16_t current, uint16_t target, int32_t step)
{
    int32_t a, b;
    if (current == target) return current;
    a = (int16_t)current + 0x10000;
    b = (int16_t)target + 0x10000;
    if (a >= b + 0x8000) a %= 0x10000;
    if (b >= a + 0x8000) b %= 0x10000;
    if (a < b) {
        a += step;
        if (a > b) a = b;
    } else {
        a -= step;
        if (a < b) a = b;
    }
    return (uint16_t)a;
}
