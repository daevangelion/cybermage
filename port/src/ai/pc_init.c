#include "ai.h"
#include "events.h"

#include <string.h>

/* Xox::InitPC, CM_HI.EXE 0x10678. Called by boot (0x10FBA) and new
 * game (0x11058/0x110D7), not by ordinary scene transitions. */
int ai_init_pc(events_state *e, sim_state *s, world_rng *rng,
                const cm_exe *exe, unsigned start_scene, uint32_t timer_ticks)
{
    int32_t ammo_size[12];
    unsigned i;
    if (cm_exe_table(exe, 0x93ECC, ammo_size, 12)) return -1;
    e->PCBetOdds = world_rnd(rng, 4) + 2;
    e->PCBet = 0; e->PCBetPick = -1; e->JackPot = 10000;
    e->SlotWaitTime = 0; e->SlotVisitTime = (int32_t)timer_ticks;
    memset(e->items, 0, sizeof(e->items));
    memset(e->ammo, 0, sizeof(e->ammo));
    memset(e->spells, 0, sizeof(e->spells));
    e->weapon = 0; e->hh_weapon = 0; e->spell = 0;
    e->item_spell = 0; e->weapon_slot = 0; e->hh_dice = 0;
    for (i = 0; i < 12; ++i) e->max_ammo[i] = (int16_t)ammo_size[i];
    for (i = 0; i < EVENTS_ARMOR_MAX; ++i) e->armor[i] = e->max_armor[i] = 30;
    e->energy = e->max_energy = start_scene == 11 ? 100 : 40;
    e->cash = start_scene == 11 ? 60 : 85;
    /* GiveKey(120,-1): definition metadata is resolved by Events' scene
     * loader before gameplay. Original quantity at +130D80 is zero. */
    e->items[0].type = 120;
    e->spells[0] = 1;
    e->SAVEStatus = 0;
    e->SAVELife = e->SAVELifeMax = (int16_t)(start_scene == 11 ? 100 : 40);
    s->pc_mode = 5;
    return 0;
}

/* SavePCInfo 0x1B8CC / RestorePCInfo 0x1B8F8; LoadScene inlines the latter
 * at 0x11FBC..0x11FE5 after LoadPERSONS rolls every actor's HP. */
void ai_save_pc_info(events_state *e, const world *w)
{
    const world_person *p = &w->persons[0];
    e->SAVEStatus = p->status;
    e->SAVELife = (int16_t)p->unk_0038;
    e->SAVELifeMax = (int16_t)((uint32_t)p->unk_0038 >> 16);
}

void ai_restore_pc_info(const events_state *e, world *w)
{
    world_person *p = &w->persons[0];
    p->status = e->SAVEStatus;
    p->unk_0038 = (int32_t)((uint32_t)(uint16_t)e->SAVELifeMax << 16 |
                           (uint16_t)e->SAVELife);
}
