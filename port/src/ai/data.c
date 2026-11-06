#include "ai.h"

#include <string.h>

/* CM_HI.EXE LoadMONSTER 0x231C0; disk -> runtime mapping in docs/re/ai.md. */
static ai_dice dice(const uint8_t *p)
{
    ai_dice d;
    d.count = p[0];
    d.sides = p[1];
    d.add = (int16_t)cm_u16(p + 2);
    return d;
}

int ai_monster_decode(ai_monster *m, const uint8_t *p, uint32_t size)
{
    unsigned i;
    if (size != AI_MONSTER_DISK_SIZE)
        return cm_fail("MONSTER", size, "invalid monster record size");
    memset(m, 0, sizeof(*m));
    m->gsx = (int16_t)cm_u16(p + 0x17);
    memcpy(m->attack, p + 0x41, sizeof(m->attack));
    m->unk_002C = p[0x19]; m->unk_002D = p[0x1A];
    m->unk_002E = (int16_t)cm_u16(p + 0x1B);
    m->range = (int32_t)cm_u32(p + 0xC2);
    m->turn_speed = (int32_t)cm_u32(p + 0xC6);
    m->accuracy = p[0xCA]; m->aggression = p[0xCB];
    m->kind = p[0x15]; m->faction = p[0x16];
    for (i = 0; i < AI_FACTIONS; ++i) m->rank[i] = (int8_t)p[0x1D + i];
    for (i = 0; i < 5; ++i) m->sounds[i] = cm_u16(p + 0xCC + i * 2);
    for (i = 0; i < 3; ++i) m->sounds[i + 5] = p[0x7D + i];
    m->sound_range = (int32_t)p[0x82] << 12;
    if (m->sound_range <= 0) m->sound_range = 0x40000;
    m->melee_range = (int32_t)cm_u32(p + 0xD6);
    m->unk_0060 = (int32_t)cm_u32(p + 0xDA);
    m->unk_0064 = (int16_t)cm_u16(p + 0xDE);
    m->unk_0066 = (int16_t)cm_u16(p + 0xE0);
    m->unk_0068 = (int16_t)cm_u16(p + 0xE2);
    m->unk_006A = (int16_t)cm_u16(p + 0xE4);
    m->unk_006C = p[0xE6]; m->melee_damage = dice(p + 0xE7);
    m->fire_range = (int32_t)cm_u32(p + 0xEB);
    m->unk_0078 = (int32_t)cm_u32(p + 0xEF);
    m->unk_007C = (int16_t)cm_u16(p + 0xF3);
    m->unk_007E = (int16_t)cm_u16(p + 0xF5);
    m->fire = p[0xF7]; m->unk_0081 = p[0xF8];
    m->unk_0082 = p[0xF9]; m->unk_0083 = p[0xFA];
    m->unk_0084 = (int16_t)cm_u16(p + 0xFB);
    m->unk_0088 = (int32_t)cm_u32(p + 0xFD);
    m->unk_008C = (int32_t)cm_u32(p + 0x101);
    m->unk_0090 = (int32_t)cm_u32(p + 0x105);
    m->unk_0094 = (int16_t)cm_u16(p + 0x109);
    m->unk_0096 = (int16_t)cm_u16(p + 0x10B);
    m->throw_type = p[0x10D]; m->unk_0099 = p[0x10E];
    m->unk_009A = p[0x10F]; m->unk_009B = p[0x110];
    m->unk_009C = (int16_t)cm_u16(p + 0x111);
    for (i = 0; i < 5; ++i) m->unk_009E[i] = dice(p + 0x2D + i * 4);
    for (i = 0; i < AI_SPELL_MAX; ++i) {
        if ((p[0x94 + i / 8] & (1u << (i % 8))) == 0) continue;
        if (m->spell_count == sizeof(m->spells))
            return cm_fail("MONSTER", 0x94 + i / 8, "too many monster spells");
        m->spells[m->spell_count++] = (uint8_t)i;
    }
    memcpy(m->resistance, p + 0xB0, sizeof(m->resistance));
    m->unk_00D0 = (int32_t)p[0x86] << 12;
    m->flags = p[0x113];
    return 0;
}

/* LoadANIMOBJ 0x23DCC. Only simulation columns; no sprite pixels. */
int ai_sequence_decode(ai_sequence *s, const uint8_t *p, uint32_t size)
{
    unsigned i;
    if (size != AI_SEQUENCE_DISK_SIZE)
        return cm_fail("ANIMOBJ", size, "invalid animation record size");
    memcpy(s->start, p + 0x16, sizeof(s->start));
    memcpy(s->step, p + 0x34, sizeof(s->step));
    for (i = 0; i < AI_ANIM_STATES; ++i)
        if (s->start[i] >= AI_ANIM_STEPS)
            return cm_fail("ANIMOBJ", 0x16 + i, "animation start outside sequence");
    for (i = 0; i < AI_ANIM_STEPS; ++i)
        if (s->step[i] != 255 && s->step[i] >= AI_ANIM_FRAMES)
            return cm_fail("ANIMOBJ", 0x34 + i, "animation frame outside table");
    for (i = 0; i < AI_ANIM_FRAMES; ++i) {
        s->radius[i] = (int32_t)cm_u32(p + 0x84 + i * 4);
        s->height[i] = (int32_t)cm_u32(p + 0xD4 + i * 4);
        s->unk_011C[i] = (int32_t)cm_u32(p + 0x124 + i * 4);
        s->unk_016C[i] = (int32_t)cm_u32(p + 0x174 + i * 4);
        s->speed[i] = (int32_t)cm_u32(p + 0x1C4 + i * 4);
        s->vertical[i] = (int32_t)cm_u32(p + 0x214 + i * 4);
        s->image[i] = p[0x264 + i];
    }
    return 0;
}

int ai_load_scene_definitions(ai_state *a, world *w)
{
    const uint8_t *p;
    uint32_t size;
    unsigned i;
    if (rsc_entry(&w->scenes, 98, &p, &size)) return -1;
    if (size % AI_MONSTER_DISK_SIZE || size / AI_MONSTER_DISK_SIZE > AI_MONSTER_MAX)
        return cm_fail(w->scenes.path, 98, "invalid MONSTER database");
    a->monster_count = size / AI_MONSTER_DISK_SIZE;
    for (i = 0; i < a->monster_count; ++i)
        if (ai_monster_decode(&a->monsters[i], p + i * AI_MONSTER_DISK_SIZE,
                              AI_MONSTER_DISK_SIZE)) return -1;
    if (rsc_entry(&w->scenes, 99, &p, &size)) return -1;
    if (size % AI_GSX_DISK_SIZE || size / AI_GSX_DISK_SIZE > AI_GSX_MAX)
        return cm_fail(w->scenes.path, 99, "invalid GSX database");
    a->gsx_count = size / AI_GSX_DISK_SIZE;
    for (i = 0; i < a->gsx_count; ++i)
        a->gsx_sequence[i] = (int16_t)cm_u16(p + i * AI_GSX_DISK_SIZE);
    if (rsc_entry(&w->scenes, 100, &p, &size)) return -1;
    if (size % AI_SEQUENCE_DISK_SIZE || size / AI_SEQUENCE_DISK_SIZE > AI_SEQUENCE_MAX)
        return cm_fail(w->scenes.path, 100, "invalid ANIMOBJ database");
    a->sequence_count = size / AI_SEQUENCE_DISK_SIZE;
    for (i = 0; i < a->sequence_count; ++i)
        if (ai_sequence_decode(&a->sequences[i], p + i * AI_SEQUENCE_DISK_SIZE,
                               AI_SEQUENCE_DISK_SIZE)) return -1;
    return 0;
}

int ai_init(ai_state *a, world *w)
{
    memset(a, 0, sizeof(*a));
    a->pc_sound = -1;
    a->pc_learn_spell = -1;
    a->difficulty = 1; /* CM_HI.EXE playDiffLevel initial data at 0x925E8. */
    if (ai_load_scene_definitions(a, w) || ai_init_persons(a, w) ||
        ai_init_missiles(a, w)) return -1;
    return ai_init_soul_fire(a, w);
}

int32_t ai_do_dice(world_rng *rng, ai_dice d)
{
    int32_t result = d.add;
    unsigned i;
    for (i = 0; i < d.count; ++i)
        result += world_rnd(rng, d.sides) + 1;
    return result;
}
