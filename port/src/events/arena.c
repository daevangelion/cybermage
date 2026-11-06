#include "events.h"
#include "ai.h"

/* CM_HI.EXE LoadArenaScreen4B579..4B924, actual fight, not a random payout. */
int events_arena_start(events_state *e, world *w)
{
    static const unsigned types[4]={20,5,4,5};
    static const int32_t xs[4]={0x1B2000,0x1BC000,0x1B8000,0x1B4000};
    static const int32_t ys[4]={0xE8000,0xAC000,0xA4000,0xAC000};
    unsigned i;
    int bonus;
    if (!e || !w || w->scene!=2)
        return cm_fail("arena",0,"arena requires scene two");
    if (!e->actors || e->event_count<243 || w->person_count<61 ||
        e->PCBet<=0 || (e->PCBetPick!=0 && e->PCBetPick!=1) ||
        e->PCBetOdds<2 || e->PCBetOdds>32767)
        return cm_fail("arena",0,"missing arena actors/events or invalid bet");
    for (i=57;i<61;++i)
        if (w->persons[i].object<=0 || (unsigned)w->persons[i].object>=w->object_count)
            return cm_fail("arena",i,"invalid arena actor object");
    for (i=0;i<4;++i) {
        world_omap_delete(w,(unsigned)w->persons[i+57].object);
        if (ai_set_person(e->actors,w,i+57,types[i],99,xs[i],ys[i],0,0,0)) return -1;
        world_omap_insert(w,(unsigned)w->persons[i+57].object);
    }
    e->events[24].enabled=e->events[25].enabled=e->events[66].enabled=1;
    e->events[24].value[3]=e->events[25].value[3]=e->events[66].value[3]=57;
    e->events[28].value[3]=59; e->events[243].value[1]=0;
    bonus=(e->PCBetOdds-2)*(e->PCBetPick?15:5);
    for (i=e->PCBetPick?57u:58u;i<(e->PCBetPick?58u:61u);++i) {
        world_person *p=&w->persons[i];
        uint16_t hp=(uint16_t)((uint16_t)p->unk_0038+bonus);
        p->unk_0038=(int32_t)(((uint32_t)p->unk_0038&UINT32_C(0xffff0000))|hp);
    }
    return 0;
}
