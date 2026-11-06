#include "ai.h"

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
