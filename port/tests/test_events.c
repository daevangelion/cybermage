#include "events.h"
#include "ai.h"
#include <assert.h>
#include <string.h>

static world w;
static events_state e;
static sim_state s;
static ai_state a;
static events_state restored;
static ui_save_state saved;

int main(void)
{
    unsigned i;
    memset(&w, 0, sizeof(w));
    memset(&e, 0, sizeof(e));
    memset(&s, 0, sizeof(s));
    memset(&a, 0, sizeof(a));
    w.object_count = 10;
    w.person_count = 85;
    w.player_object = 1;
    w.player_person = 0;
    w.persons[0].object = 1;
    w.persons[0].unk_0038 = 40 << 16 | 10;
    e.items[0].type = 9;
    e.items[0].kind = 9;
    e.items[0].quantity = 2;
    assert(events_use_item(&e, &s, &w, 0) == 0);
    assert((w.persons[0].unk_0038 & 65535) == 40);
    assert(e.items[0].quantity == 1);
    assert(events_use_item(&e, &s, &w, 0) == 0);
    assert(e.items[0].quantity == 1);
    assert(events_use_item(&e, &s, &w, EVENTS_ITEM_MAX) < 0);
    e.items[1].type = 11;
    e.items[1].kind = 11;
    e.items[1].quantity = 1;
    e.armor[0] = 3; e.max_armor[0] = 20;
    assert(events_use_item(&e, &s, &w, 1) == 0);
    assert(e.armor[0] == 20 && e.items[1].type == 0);
    assert(events_find_auto_slot(&e) == 0);
    for (i = 0; i < EVENTS_AUTO_MAX; ++i) e.auto_slot[i] = 1;
    assert(events_find_auto_slot(&e) == -1);
    memset(e.items, 0, sizeof(e.items));
    e.items[0].type = 5; e.items[0].kind = 5;
    e.items[1].type = 4; e.items[1].kind = 6;
    assert(events_make_selector(&e, 1) == 0);
    assert(e.selector_count == 2);
    assert(e.selector_items[0] == 1 && e.selector_items[1] == 0);
    memset(e.items, 0, sizeof(e.items));
    e.spells[0] = 1;
    assert(events_make_selector(&e, 2) == 0);
    assert(e.selector_count == 1 && e.selector_items[0] == 0);
    w.scene = 1;
    w.persons[84].object = 2; w.persons[84].type = 1;
    a.monster_count = 2; a.monsters[1].kind = 5;
    w.objects[1].height = 0x10000;
    w.objects[2].z = 0x4000;
    w.objects[2].x = 0x7000;
    assert(events_switch_search(&e, &s, &a, &w) == 0);
    assert(!e.tank_flag);
    w.objects[2].x = 0x4000;
    w.objects[1].heading = cm_atan64k(0, 0, 0x4000, 0);
    assert(events_switch_search(&e, &s, &a, &w) == 0);
    assert(e.tank_flag && e.vehicle_person == 84 && e.vehicle_type == 0);
    memset(e.items,0,sizeof(e.items));
    e.definition_count=2;e.definitions[1].kind=9;
    assert(events_buy_item(&e,&w,1)==1);
    assert(events_buy_item(&e,&w,1)==1);
    assert(e.items[0].quantity==2);
    assert(events_inventory_sell(&e,0,1)==1);
    assert(e.items[0].quantity==1);
    e.events[511].value[15]=-1234567;e.events[511].state=7;
    e.cash=999;e.energy=12;e.max_energy=40;e.ammo[11]=37;
    assert(events_save_export(&e,&saved)==0);
    restored.definition_count=2;
    assert(events_save_import(&restored,&saved)==0);
    assert(restored.items[0].type==1 && restored.items[0].quantity==1);
    assert(restored.events[511].value[15]==-1234567 && restored.events[511].state==7);
    assert(restored.cash==999 && restored.energy==12 && restored.ammo[11]==37);
    w.objects[3].x=w.objects[1].x;w.objects[3].y=w.objects[1].y;
    w.objects[3].radius=w.objects[3].height=65536;w.objects[3].z=0;
    w.objects[1].floor_z=65536;
    events_zflat_drop(&w,3,65536);
    assert(w.objects[1].floor_z==65536-0x4000);
    assert(events_rescale_prop(&w,0,100)<0);
    return 0;
}
