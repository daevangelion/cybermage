#include "ai.h"

/* SaveGame 0x5007C: twelve 0x30 missile records, ten 0x1C SoulFire records.
 * These are numeric fields, never native struct/pointer dumps. */
int ai_save_import(ai_state *a, world *w, const ui_save_state *save)
{
    unsigned i;
    for (i = 0; i < AI_MISSILE_MAX; ++i) {
        const uint32_t *p = save->unk_135ECC + i * 12;
        ai_missile *m = &a->missiles[i];
        m->active = (int32_t)p[0]; m->object = (int32_t)p[1];
        m->vertical = (int32_t)p[2]; m->ticks = (int32_t)p[3];
        m->type = (int32_t)p[4]; m->target = (int32_t)p[5];
        m->source = (int32_t)p[6]; m->frame = (int32_t)p[7];
        m->sound = (int32_t)p[8]; m->speed = (int32_t)p[9];
        m->variant = (int32_t)p[10]; m->radius = (int32_t)p[11];
        if (m->object < 0 || (unsigned)m->object >= w->object_count ||
            (m->active && (m->type < 0 || m->type >= AI_MISSILE_TYPES)))
            return cm_fail("SAVE*.DAT", i * 48, "invalid saved missile");
    }
    for (i = 0; i < AI_SOUL_MAX; ++i) {
        const uint32_t *p = save->unk_13610C + i * 7;
        ai_soul *s = &a->souls[i];
        s->active = (int32_t)p[0]; s->object = (int32_t)p[1];
        s->fxo = (int32_t)p[2]; s->type = (int32_t)p[3];
        s->value = (int32_t)p[4]; s->lifetime = (int32_t)p[5]; s->frame = (int32_t)p[6];
        if (s->object < 0 || (unsigned)s->object >= w->object_count ||
            s->fxo < 0 || s->fxo >= 2048)
            return cm_fail("SAVE*.DAT", i * 28, "invalid saved SoulFire");
        w->fxo_objects[s->fxo] = (uint16_t)s->object;
        w->fxo_types[s->fxo] = -1;
        if ((unsigned)s->fxo >= w->fxo_count) w->fxo_count = (unsigned)s->fxo + 1;
    }
    a->gem_count_down = (int32_t)save->gemCountDown;
    a->gem_count_down_timer = (int32_t)save->gemCountDownTimer;
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
        p[2] = (uint32_t)s->fxo; p[3] = (uint32_t)s->type;
        p[4] = (uint32_t)s->value; p[5] = (uint32_t)s->lifetime; p[6] = (uint32_t)s->frame;
    }
    save->gemCountDown = (uint32_t)a->gem_count_down;
    save->gemCountDownTimer = (uint32_t)a->gem_count_down_timer;
}
