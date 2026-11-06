#include "ai.h"
#include <limits.h>
#include <string.h>

static int32_t signed_word(uint32_t value)
{
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

/* Explicit verification input, never scene initialization or game-save loading.
 * CMPERSON records are index, ignored original address, then 0xC0 disk bytes.
 * Import only the original uninitialized field, not positions, HP or RNG. */
int ai_import_person_undefined(world *w, const uint8_t *data, uint32_t size)
{
    enum { HEADER = 12, RECORD = 200, PERSON_DATA = 8, UNDEFINED = 0x24 };
    uint32_t count, i;
    if (!w || !data || size < HEADER || memcmp(data, "CMPERSON", 8))
        return cm_fail("oracle PERSON", 0, "invalid snapshot header");
    count = cm_u32(data + 8);
    if (count != w->person_count || count > CM_OBJECT_MAX ||
        size != HEADER + count * RECORD)
        return cm_fail("oracle PERSON", 8, "snapshot actor count/size mismatch");
    /* Validate every identity before mutating any actor. */
    for (i = 0; i < count; ++i) {
        const uint8_t *record = data + HEADER + i * RECORD;
        int object = w->persons[i].object;
        if (object < 0 || (unsigned)object >= w->object_count ||
            cm_u32(record) != i || cm_u16(record + PERSON_DATA) != (unsigned)object)
            return cm_fail("oracle PERSON", HEADER + i * RECORD, "snapshot actor identity mismatch");
    }
    for (i = 0; i < count; ++i)
        w->persons[i].unk_0024 = signed_word(cm_u32(data + HEADER + i * RECORD +
                                                   PERSON_DATA + UNDEFINED));
    return 0;
}

static int saved_pc_valid(const uint32_t *p)
{
    /* CM_HI.EXE ProcessInputEvents1481E copies cooldown into latch;
     * HandleNextFrame1D841/1DF9E bounds cooldown/link ticks. Init1097E
     * enables auto-aim; tank manual clamps1D237..1D28F and the auto
     * path in PCLaunchMissile2F1C4 uses the same bounds.
     * Aim X/Y are signed32, not indices: PCLaunchMissile2F1C4 wraps
     * their arithmetic. No fabricated int16 bound on transient aim. */
    static const unsigned indices[] = {6, 7, 8, 9, 10, 12, 15};
    static const int32_t hi[] = {1, 3, 3, 1, 5, 1, 63};
    unsigned i;
    for (i = 0; i < sizeof indices / sizeof indices[0]; ++i) {
        unsigned index = indices[i];
        int32_t value = signed_word(p[index]);
        if (value < 0 || value > hi[i])
            return cm_fail("SAVE*.DAT", 0x6D4 + index * 4, "invalid saved player combat state");
    }
    return 0;
}

/* SaveGame 0x5007C: twelve 0x30 missile records, ten 0x1C SoulFire records.
 * These are numeric fields, never native struct/pointer dumps. */
int ai_save_import(ai_state *a, world *w, const ui_save_state *save)
{
    unsigned i;
    if (saved_pc_valid(save->unk_6D4)) return -1;
    /* Validate against the pools constructed by scene init, before changing
     * their ownership or any live state. Saved pool mappings are not trusted. */
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        const uint32_t *p = save->unk_135ECC + i * 12;
        if (p[1] != (uint32_t)a->missiles[i].object ||
            p[1] == 0 || p[1] >= w->object_count ||
            (p[0] && (p[4] < 1 || p[4] >= AI_MISSILE_TYPES)) || p[7] >= 8)
            return cm_fail("SAVE*.DAT", 0x2C551 + i * 48, "invalid saved missile");
    }
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        const uint32_t *p = save->unk_13610C + i * 7;
        if (p[1] != (uint32_t)a->souls[i].object ||
            p[2] != (uint32_t)a->souls[i].prop ||
            p[1] == 0 || p[1] >= w->object_count || p[2] >= w->prop_count ||
            p[5] > INT32_MAX || p[6] >= 8)
            return cm_fail("SAVE*.DAT", 0x2C791 + i * 28, "invalid saved SoulFire");
    }
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        const uint32_t *p = save->unk_135ECC + i * 12;
        ai_missile *m = &a->missiles[i];
        m->active = (int32_t)p[0]; m->object = (int32_t)p[1];
        m->vertical = (int32_t)p[2]; m->ticks = (int32_t)p[3];
        m->type = (int32_t)p[4]; m->target = (int32_t)p[5];
        m->source = (int32_t)p[6]; m->frame = (int32_t)p[7];
        m->sound = (int32_t)p[8]; m->speed = (int32_t)p[9];
        m->variant = (int32_t)p[10]; m->radius = (int32_t)p[11];
    }
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        const uint32_t *p = save->unk_13610C + i * 7;
        ai_soul *s = &a->souls[i];
        s->active = (int32_t)p[0]; s->object = (int32_t)p[1];
        s->prop = (int32_t)p[2]; s->type = (int32_t)p[3];
        s->value = (int32_t)p[4]; s->lifetime = (int32_t)p[5]; s->frame = (int32_t)p[6];
    }
    a->gem_count_down = (int32_t)save->gemCountDown;
    a->gem_count_down_timer = signed_word(a->timer_ticks + save->gemCountDownTimer);
    a->pc.fire = signed_word(save->unk_6D4[6]);
    a->pc.cooldown = signed_word(save->unk_6D4[7]);
    a->pc.latch = signed_word(save->unk_6D4[8]);
    a->unk_144AE0 = signed_word(save->unk_6D4[9]);
    a->pc.magic_link_ticks = signed_word(save->unk_6D4[10]);
    /* CM_HI.EXE EventsAction72 3E9D7..3E9FA writes signed payloads,
     * consumed as nonzero rather than restricted to a boolean. */
    a->pc.underwater = signed_word(save->unk_6D4[11]);
    a->pc_aim.auto_enable = signed_word(save->unk_6D4[12]);
    a->pc_aim.aim_x = signed_word(save->unk_6D4[13]);
    a->pc_aim.aim_y = signed_word(save->unk_6D4[14]);
    a->pc_aim.tank_aim_y = signed_word(save->unk_6D4[15]);
    return 0;
}

void ai_save_export(const ai_state *a, ui_save_state *save)
{
    unsigned i;
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        uint32_t *p = save->unk_135ECC + i * 12;
        const ai_missile *m = &a->missiles[i];
        p[0] = (uint32_t)m->active; p[1] = (uint32_t)m->object;
        p[2] = (uint32_t)m->vertical; p[3] = (uint32_t)m->ticks;
        p[4] = (uint32_t)m->type; p[5] = (uint32_t)m->target;
        p[6] = (uint32_t)m->source; p[7] = (uint32_t)m->frame;
        p[8] = (uint32_t)m->sound; p[9] = (uint32_t)m->speed;
        p[10] = (uint32_t)m->variant; p[11] = (uint32_t)m->radius;
    }
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        uint32_t *p = save->unk_13610C + i * 7;
        const ai_soul *s = &a->souls[i];
        p[0] = (uint32_t)s->active; p[1] = (uint32_t)s->object;
        p[2] = (uint32_t)s->prop; p[3] = (uint32_t)s->type;
        p[4] = (uint32_t)s->value; p[5] = (uint32_t)s->lifetime; p[6] = (uint32_t)s->frame;
    }
    save->gemCountDown = (uint32_t)a->gem_count_down;
    save->gemCountDownTimer = a->gem_count_down ?
        (uint32_t)a->gem_count_down_timer - a->timer_ticks : 0;
    save->unk_6D4[6] = (uint32_t)a->pc.fire;
    save->unk_6D4[7] = (uint32_t)a->pc.cooldown;
    save->unk_6D4[8] = (uint32_t)a->pc.latch;
    save->unk_6D4[9] = (uint32_t)a->unk_144AE0;
    save->unk_6D4[10] = (uint32_t)a->pc.magic_link_ticks;
    save->unk_6D4[11] = (uint32_t)a->pc.underwater;
    save->unk_6D4[12] = (uint32_t)a->pc_aim.auto_enable;
    save->unk_6D4[13] = (uint32_t)a->pc_aim.aim_x;
    save->unk_6D4[14] = (uint32_t)a->pc_aim.aim_y;
    save->unk_6D4[15] = (uint32_t)a->pc_aim.tank_aim_y;
}
