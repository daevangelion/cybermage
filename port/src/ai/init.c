#include "ai.h"

static void combat_init(ai_state *a, world *w, world_person *p)
{
    const ai_monster *m = &a->monsters[p->type];
    ai_dice hp = {m->unk_002C, m->unk_002D, m->unk_002E};
    int16_t max_hp = (int16_t)ai_do_dice(&w->rng, hp);
    unsigned choice = 0;
    p->unk_0038 = (int32_t)((uint32_t)(uint16_t)max_hp << 16 |
                           (p->status == 2 ? 0u : (uint16_t)max_hp));
    while (choice < 4 && m->unk_009E[choice + 1].add > 0 &&
           world_rnd(&w->rng, 3) == 2) ++choice;
    p->unk_0084 = m->unk_009E[choice].count;
    p->unk_0088 = m->unk_009E[choice].sides;
    p->unk_008C = m->unk_009E[choice].add;
    p->unk_0060 = m->unk_00D0;
    /* Original LoadPERSONS leaves this accuracy counter uninitialized.
     * Normal play deliberately uses zero; see architecture_port.md. */
    p->unk_0024 = 0;
    p->unk_0064 = p->unk_0068 = p->unk_006C = p->unk_0070 = 0;
    p->unk_007C = p->unk_0080 = 0;
}

/* LoadPERSONS 0x22D3C..0x22F46, after World places the original PERSONs. */
int ai_init_persons(ai_state *a, world *w)
{
    unsigned i;
    world_span disk = w->scene_sections[3];
    for (i = 0; i < w->person_count; ++i) {
        world_person *p = &w->persons[i];
        world_object *o = &w->objects[p->object];
        const uint8_t *src;
        if (ai_person_kind(a, w, i) < 0)
            return cm_fail("LoadPERSONS", i, "invalid monster definition");
        if (p->placement_index + 1 >= disk.size / AI_PERSON_DISK_SIZE)
            return cm_fail("LoadPERSONS", i, "invalid placement index");
        src = disk.data + (p->placement_index + 1) * AI_PERSON_DISK_SIZE;
        combat_init(a, w, p);
        p->unk_00A0 = (int16_t)cm_u16(src + 0x72);
        p->unk_00B8 = (int8_t)src[0x123];
        if (p->unk_00B8 & 1) { o->flags |= UINT32_C(0x00020000); p->unk_005C = -1; }
        if (p->unk_00B8 & 16) p->water = 1;
        if (p->unk_00B8 & 32) o->flags |= UINT32_C(0x00000800);
        p->unk_0090 = src[0x124]; p->unk_0094 = src[0x125];
        p->unk_0098 = src[0x126]; p->unk_009C = 0;
    }
    return 0;
}

/* SetPERSON 0x21E54..0x21F93; no placement flags/dialogue are inherited. */
int ai_set_person(ai_state *a, world *w, unsigned person, unsigned type,
                   unsigned state, int32_t x, int32_t y, int32_t z,
                   int32_t floor_z, uint16_t heading)
{
    world_person *p;
    int16_t hp, max_hp;
    if (type >= a->monster_count)
        return cm_fail("SetPERSON", type, "invalid monster definition");
    if (world_set_person(w, person, type, state, x, y, z, floor_z, heading)) return -1;
    p = &w->persons[person];
    combat_init(a, w, p);
    /* SetMonDiffLevel 0x53C70, baseline difficulty one, live PERSON only. */
    if (p->status != 2 && a->difficulty != 1) {
        hp = (int16_t)p->unk_0038;
        max_hp = (int16_t)((uint32_t)p->unk_0038 >> 16);
        if (a->difficulty == 0) { hp /= 2; max_hp /= 2; }
        else if (a->difficulty == 2) {
            hp = (int16_t)(hp * 2 + 1); max_hp = (int16_t)(max_hp * 2 + 1);
        }
        p->unk_0038 = (int32_t)((uint32_t)(uint16_t)max_hp << 16 | (uint16_t)hp);
    }
    p->unk_0090 = 0;
    return 0;
}
