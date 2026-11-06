#include "ai.h"

static int16_t delay_word(int32_t value)
{
    int32_t low = (int32_t)((uint32_t)value & UINT16_MAX);
    return (int16_t)(low <= INT16_MAX ? low : low - 65536);
}

/* SetMonAtkIgnore 053D04 / ReCalcMonIgnore 053AF0. LoadMONSTER 023773
 * adjusts each new definition from level1, including definition0. A live
 * change passes -1 and visits definitions1..count-1, not PERSON records. */
int ai_set_monster_attack_ignore(ai_state *a, int monster, unsigned previous,
                                  unsigned difficulty)
{
    unsigned i, first, end;
    int level;
    if (previous > 2 || difficulty > 2 || a->monster_count > AI_MONSTER_MAX ||
        (monster >= 0 && (unsigned)monster >= a->monster_count))
        return cm_fail("SetMonAtkIgnore", difficulty, "invalid monster or difficulty");
    first = monster < 0 ? 1u : (unsigned)monster;
    end = monster < 0 ? a->monster_count : first + 1;
    for (level = (int)previous; level != (int)difficulty;
         level += level < (int)difficulty ? 1 : -1) {
        int multiply, divide, increment;
        if (level < (int)difficulty) {
            multiply = level == 0 ? 2 : 1;
            divide = level == 0 ? 3 : 2;
            increment = 0;
        } else {
            multiply = level == 2 ? 2 : 3;
            divide = level == 2 ? 1 : 2;
            increment = 1;
        }
        for (i = first; i < end; ++i) {
            ai_monster *m = &a->monsters[i];
            m->unk_0066 = delay_word(m->unk_0066 * multiply / divide + increment);
            m->unk_007C = delay_word(m->unk_007C * multiply / divide + increment);
            m->unk_0094 = delay_word(m->unk_0094 * multiply / divide + increment);
        }
    }
    return 0;
}

int ai_set_attack_difficulty(ai_state *a, unsigned difficulty)
{
    if (ai_set_monster_attack_ignore(a, -1, (unsigned)a->attack_difficulty,
                                      difficulty)) return -1;
    a->attack_difficulty = (int32_t)difficulty;
    return 0;
}

/* SetMonDiffLevel 0x53C70, ReCalcMonHP 0x539CC, CM_HI.EXE.
 * Cross two levels in two operations: the intermediate signed-short store
 * and the +1 for each upward step are observable. Dead NPCs are unchanged. */
int ai_set_difficulty(ai_state *a, world *w, unsigned difficulty)
{
    unsigned i;
    int level;
    if (difficulty > 2 || a->difficulty < 0 || a->difficulty > 2)
        return cm_fail("SetMonDiffLevel", difficulty, "invalid difficulty");
    for (level = a->difficulty; level != (int)difficulty;
         level += level < (int)difficulty ? 1 : -1) {
        for (i = 1; i < w->person_count; ++i) {
            world_person *p = &w->persons[i];
            int16_t hp, max_hp;
            if (p->status == 2) continue;
            hp = (int16_t)p->unk_0038;
            max_hp = (int16_t)((uint32_t)p->unk_0038 >> 16);
            if (level < (int)difficulty) {
                hp = (int16_t)(hp * 2 + 1); max_hp = (int16_t)(max_hp * 2 + 1);
            } else { hp /= 2; max_hp /= 2; }
            p->unk_0038 = (int32_t)((uint32_t)(uint16_t)max_hp << 16 | (uint16_t)hp);
        }
    }
    a->difficulty = (int32_t)difficulty;
    return 0;
}
