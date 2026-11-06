#include "sim.h"
#include "ui.h"
#include <limits.h>
#include <string.h>

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
    /* Input requests are bounded by768; physical heights/stance remain inside
     * the four-level map envelope. Reject before arithmetic or state mutation. */
    static const struct { unsigned index; int32_t low, high; } ranges[] = {
        {SAVE_MODE,0,7}, {SAVE_PREVIOUS_MODE,0,7}, {SAVE_VEHICLE,0,1},
        {SAVE_MOMENTUM,-768,768}, {SAVE_CURRENT_SPEED,-768,768},
        {SAVE_BOB_STATE,0,2}, {SAVE_ACCELERATION,0,96},
        {SAVE_JUMP_INDEX,0,SIM_JUMP_STEPS}, {SAVE_JUMP_SPEED,-768,768},
        {SAVE_JUMP_HEIGHT,0,65536}, {SAVE_JUMP_FLOOR,0,CM_MAP_LEVELS*65536-1},
        {SAVE_FLIGHT,0,3}, {SAVE_FALL,0,1},
        {SAVE_CROUCH,-CM_MAP_LEVELS*65536,0},
        {SAVE_BODY,0,CM_MAP_LEVELS*65536}, {SAVE_SWIM,0,1}
    };
    unsigned i;
    for (i = 0; i < sizeof ranges / sizeof ranges[0]; ++i) {
        int32_t value = signed_word(v[ranges[i].index]);
        if (value < ranges[i].low || value > ranges[i].high)
            return cm_fail("SAVE*.DAT", 0x6d4 + 4 * ranges[i].index,
                           "saved simulation value outside physical bounds");
    }
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

int sim_save_fire_import(sim_state *s, const uint32_t v[4])
{
    if (v[0] > 1) return cm_fail("SAVE*.DAT", 0x34f09, "invalid saved fire flag");
    if (v[1] > UINT8_MAX) return cm_fail("SAVE*.DAT", 0x34f0d, "invalid saved fire panel");
    if (v[2] >= CM_PANEL_MAX) return cm_fail("SAVE*.DAT", 0x34f11, "invalid saved fire art");
    if (v[3] > INT32_MAX / 7)
        return cm_fail("SAVE*.DAT", 0x34f15, "saved fire damage overflows seven-die scale");
    s->fire_check = (int32_t)v[0];
    s->fire_tile_panel = (int32_t)v[1];
    s->fire_tile_art = (int32_t)v[2];
    s->fire_damage = (int32_t)v[3];
    return 0;
}

/* AutoSave 051733..051946 selects these ranges from SaveGame's byte layout.
 * Reuse the explicit UI codec: neither format contains native structs. */
enum {
    SAVE_WORLD_WORDS = 0x758, SAVE_OBJECTS = 0x201f,
    SAVE_OBJECTS_END = 0x2c551, SAVE_LIGHTS = 0x2c8a9,
    AUTO_OBJECTS = 16,
    AUTO_LIGHTS = AUTO_OBJECTS + SAVE_OBJECTS_END - SAVE_OBJECTS
};

int sim_auto_pack(const uint8_t *data, uint32_t size, uint8_t *out,
                    uint32_t capacity, uint32_t *written)
{
    uint32_t count, need;
    if (!data || !out || !written || size < UI_SAVE_PREFIX)
        return cm_fail("AUTO*.DAT", size, "truncated source save");
    count = cm_u32(data + UI_SAVE_PREFIX - 4);
    if (cm_u32(data) >= 12 || count > CM_OBJECT_MAX ||
        size != UI_SAVE_PREFIX + count * UI_PERSON_BYTES)
        return cm_fail("AUTO*.DAT", size, "invalid source save layout");
    need = SIM_AUTO_PREFIX + count * UI_PERSON_BYTES;
    if (capacity < need)
        return cm_fail("AUTO*.DAT", capacity, "auto output buffer too small");
    memcpy(out, data, 8);
    memcpy(out + 8, data + SAVE_WORLD_WORDS, 8);
    memcpy(out + AUTO_OBJECTS, data + SAVE_OBJECTS, SAVE_OBJECTS_END - SAVE_OBJECTS);
    memcpy(out + AUTO_LIGHTS, data + SAVE_LIGHTS, size - SAVE_LIGHTS);
    *written = need;
    return 0;
}

int sim_auto_unpack(uint8_t *save, uint32_t size, const uint8_t *data,
                      uint32_t length)
{
    uint32_t count;
    if (!save || !data || size < UI_SAVE_PREFIX || length < SIM_AUTO_PREFIX)
        return cm_fail("AUTO*.DAT", length, "truncated auto save");
    count = cm_u32(data + SIM_AUTO_PREFIX - 4);
    if (count > CM_OBJECT_MAX || length != SIM_AUTO_PREFIX + count * UI_PERSON_BYTES ||
        size != UI_SAVE_PREFIX + count * UI_PERSON_BYTES ||
        cm_u32(save + UI_SAVE_PREFIX - 4) != count || cm_u32(data) != cm_u32(save))
        return cm_fail("AUTO*.DAT", length, "auto save scene or layout mismatch");
    memcpy(save, data, 8);
    memcpy(save + SAVE_WORLD_WORDS, data + 8, 8);
    memcpy(save + SAVE_OBJECTS, data + AUTO_OBJECTS, SAVE_OBJECTS_END - SAVE_OBJECTS);
    memcpy(save + SAVE_LIGHTS, data + AUTO_LIGHTS, size - SAVE_LIGHTS);
    return 0;
}
