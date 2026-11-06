#include "sim.h"
#include <limits.h>

/* SaveGame/LoadGame's 29 discontiguous words at file06D4. Unknown fields
 * belonging to UI/AI/Events stay untouched; this is not a struct dump. */
enum {
    SAVE_MODE = 0, SAVE_PREVIOUS_MODE = 1, SAVE_VEHICLE = 5,
    SAVE_MOMENTUM = 16, SAVE_CURRENT_SPEED = 17, SAVE_BOB_STATE = 18,
    SAVE_ACCELERATION = 19, SAVE_JUMP_INDEX = 20, SAVE_JUMP_SPEED = 21,
    SAVE_JUMP_HEIGHT = 22, SAVE_JUMP_FLOOR = 23, SAVE_FLIGHT = 24,
    SAVE_FALL = 25, SAVE_CROUCH = 26, SAVE_BODY = 27, SAVE_SWIM = 28
};

static int32_t signed_word(uint32_t value)
{
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

void sim_save_export(const sim_state *s, uint32_t v[29])
{
    v[SAVE_MODE] = (uint32_t)s->pc_mode;
    v[SAVE_PREVIOUS_MODE] = (uint32_t)s->previous_pc_mode;
    v[SAVE_VEHICLE] = (uint32_t)s->vehicle_type;
    v[SAVE_MOMENTUM] = (uint32_t)s->unk_144ae8;
    v[SAVE_CURRENT_SPEED] = (uint32_t)s->unk_144af8;
    v[SAVE_BOB_STATE] = (uint32_t)s->unk_144afc;
    v[SAVE_ACCELERATION] = (uint32_t)s->unk_144b00;
    v[SAVE_JUMP_INDEX] = (uint32_t)s->unk_144b1c;
    v[SAVE_JUMP_SPEED] = (uint32_t)s->unk_144b24;
    v[SAVE_JUMP_HEIGHT] = (uint32_t)s->unk_144b28;
    v[SAVE_JUMP_FLOOR] = (uint32_t)s->unk_144b2c;
    v[SAVE_FLIGHT] = (uint32_t)s->flight_mode;
    v[SAVE_FALL] = (uint32_t)s->unk_144b30;
    v[SAVE_CROUCH] = (uint32_t)s->unk_144b34;
    v[SAVE_BODY] = (uint32_t)s->body_height;
    v[SAVE_SWIM] = (uint32_t)s->unk_144b50;
}

int sim_save_import(sim_state *s, const uint32_t v[29])
{
    if (v[SAVE_MODE] > 7 || v[SAVE_PREVIOUS_MODE] > 7 || v[SAVE_FLIGHT] > 3 ||
        v[SAVE_JUMP_INDEX] > SIM_JUMP_STEPS)
        return cm_fail("SAVE*.DAT", 0x6d4, "invalid saved simulation mode or jump index");
    s->pc_mode = signed_word(v[SAVE_MODE]);
    s->previous_pc_mode = signed_word(v[SAVE_PREVIOUS_MODE]);
    s->vehicle_type = signed_word(v[SAVE_VEHICLE]);
    s->unk_144ae8 = signed_word(v[SAVE_MOMENTUM]);
    s->unk_144af8 = signed_word(v[SAVE_CURRENT_SPEED]);
    s->unk_144afc = signed_word(v[SAVE_BOB_STATE]);
    s->unk_144b00 = signed_word(v[SAVE_ACCELERATION]);
    s->unk_144b1c = signed_word(v[SAVE_JUMP_INDEX]);
    s->unk_144b24 = signed_word(v[SAVE_JUMP_SPEED]);
    s->unk_144b28 = signed_word(v[SAVE_JUMP_HEIGHT]);
    s->unk_144b2c = signed_word(v[SAVE_JUMP_FLOOR]);
    s->flight_mode = signed_word(v[SAVE_FLIGHT]);
    s->unk_144b30 = signed_word(v[SAVE_FALL]);
    s->unk_144b34 = signed_word(v[SAVE_CROUCH]);
    s->body_height = signed_word(v[SAVE_BODY]);
    s->unk_144b50 = signed_word(v[SAVE_SWIM]);
    return 0;
}
