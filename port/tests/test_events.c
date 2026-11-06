#include "events.h"
#include "ai.h"
#include "audio.h"
#include <assert.h>
#include <string.h>

static world w;
static events_state e;
static sim_state s;
static ai_state a;
static events_state restored;
static ui_save_state saved;

static events_event *event_setup(unsigned type)
{
    memset(&w,0,sizeof(w));memset(&e,0,sizeof(e));
    memset(&s,0,sizeof(s));memset(&a,0,sizeof(a));
    w.player_object=1;w.object_count=4;w.person_count=3;
    w.persons[0].object=1;w.persons[1].object=2;w.persons[2].object=3;
    e.event_count=511;e.switch_count=4;e.event_heads[0]=1;a.events=&e;
    e.events[1].type=(uint8_t)type;e.events[1].enabled=1;e.ambient_slot=-1;
    return &e.events[1];
}

static void remaining_events(void)
{
    events_event *v;
    unsigned i;
    v=event_setup(38);v->value[0]=2;v->value[3]=3;v->value[7]=2;
    v->value[9]=1;v->value[11]=0;e.events[2].state=3;e.events[2].value[1]=1;
    assert(events_step(&e,&s,&a,&w)==0 && e.events[3].value[1]==1);
    v=event_setup(40);v->value[8]=1;v->value[11]=2;v->value[7]=3;
    v->value[3]=0;v->value[0]=2;e.switches[1].state=e.switches[2].state=1;
    e.switches[0].state=1;
    assert(events_step(&e,&s,&a,&w)==0 && e.events[2].value[1]==1);
    v=event_setup(41);v->value[3]=1;v->value[9]=1;e.switches[1].state=1;
    v->value[0]=2;v->value[11]=7;v->value[8]=3;v->value[7]=9;
    assert(events_step(&e,&s,&a,&w)==0);
    assert(e.events[2].enabled==7 && e.events[3].value[1]==9);
    v=event_setup(47);v->value[0]=2;v->value[3]=1;v->value[9]=1;
    e.switches[1].state=1;
    assert(events_step(&e,&s,&a,&w)==0 && e.events[2].enabled==1);
    e.switches[1].state=0;
    assert(events_step(&e,&s,&a,&w)==0 && !e.events[2].enabled);
    v=event_setup(48);v->value[11]=3;v->value[15]=1;v->value[1]=7;
    assert(events_step(&e,&s,&a,&w)==0 && w.apertures[3].enabled==7);
    v=event_setup(51);v->value[3]=12;e.items[0].type=7;
    e.items[0].kind=14;e.items[0].subtype=12;
    assert(events_step(&e,&s,&a,&w)==0 && !e.items[0].type && v->state==1);
    v=event_setup(53);v->value[3]=1;v->value[8]=10;v->value[9]=1;
    v->value[11]=2;w.persons[1].unk_0038=20<<16|2;
    assert(events_step(&e,&s,&a,&w)==0);
    assert((w.persons[1].unk_0038&65535)==20 && e.events[2].value[1]==1);
    v=event_setup(54);v->value[3]=1;v->value[0]=2;
    assert(events_step(&e,&s,&a,&w)==0 && e.events[2].value[1]==1);
    v->value[2]=0;v->value[6]=-1;w.objects[2].floor_z=65536;w.objects[2].z=-1;
    w.objects[1].floor_z=65536;e.events[2].value[1]=0;
    assert(events_step(&e,&s,&a,&w)==0 && e.events[2].value[1]==1);
    w.persons[1].object=CM_OBJECT_MAX;
    assert(events_step(&e,&s,&a,&w)<0);
    v=event_setup(57);v->value[3]=1;v->value[8]=3;v->value[13]=1;
    v->value[0]=2;w.persons[1].status=2;
    assert(events_step(&e,&s,&a,&w)==0 && v->state==1 && e.events[2].enabled);
    v=event_setup(72);v->value[3]=1;v->value[9]=1;v->value[8]=8;v->value[11]=9;
    assert(events_step(&e,&s,&a,&w)==0 && a.pc.underwater==9);
    e.switches[1].state=1;
    assert(events_step(&e,&s,&a,&w)==0 && a.pc.underwater==8);
    v=event_setup(42);
    assert(events_step(&e,&s,&a,&w)==0 && v->state==1);
    v=event_setup(46);v->value[2]=0;v->value[6]=0x18000;
    v->value[5]=0x28000;v->value[4]=0x10000;w.objects[2].active=1;
    assert(events_step(&e,&s,&a,&w)==0 && w.objects[2].x==0x18000);
    v=event_setup(49);v->value[8]=1;v->value[9]=1;v->value[2]=0;
    w.fxo_count=1;w.fxo_objects[0]=2;e.switches[1].fxo=0;
    assert(events_step(&e,&s,&a,&w)==0 && e.switches[1].state==1);
    v=event_setup(50);v->state=3;s.bp_limit_flag=1;
    assert(events_step(&e,&s,&a,&w)==0 && v->state==2);
    v=event_setup(56);v->value[3]=1;
    assert(events_step(&e,&s,&a,&w)==0 && v->state==1);
    assert(e.coderStr[8]>='A' && e.coderStr[8]<='D');
    assert(e.coderStr[11]>='1' && e.coderStr[11]<='4' && !e.coderStr[12]);
    v=event_setup(68);v->value[2]=0;w.person_count=62;
    for(i=3;i<62;++i) w.persons[i].object=2;
    assert(events_step(&e,&s,&a,&w)==0 && v->state==1);
    assert(w.persons[57].state==9 && e.events[24].enabled==1);
    v=event_setup(69);v->value[2]=0;w.person_count=62;
    w.persons[57].state=0;w.persons[58].status=0;
    assert(events_step(&e,&s,&a,&w)==0 && v->state==1);
    v=event_setup(70);v->value[0]=2;v->value[2]=0;v->value[3]=7;
    e.items[0].type=1;e.items[0].kind=14;e.items[0].subtype=7;
    assert(events_step(&e,&s,&a,&w)==0 && e.events[2].value[1]==1);
    v=event_setup(83);v->value[3]=0;v->value[2]=0x10000;
    v->value[6]=0x18000;v->value[5]=0x28000;v->value[7]=1;v->value[11]=2;
    w.prop_count=1;w.prop_objects[0]=2;w.objects[2].active=1;
    assert(events_step(&e,&s,&a,&w)==0 && w.objects[2].x==0x18000 && e.switches[1].key==999);
    v=event_setup(87);v->value[3]=0;s.bp_limit_flag=1;w.prop_count=1;w.prop_objects[0]=2;
    world_rng_init(&w.rng,123);
    assert(events_step(&e,&s,&a,&w)==0);
}

static void cosmetics(void)
{
    static const uint8_t panel_bytes[3]={11,22,33};
    unsigned i;
    event_setup(0);w.scene=4;w.panel_count=w.texture_count=3;
    for(i=0;i<3;++i) { e.waterNdx[i]=(int32_t)i;w.panels[i].pixels.data=panel_bytes+i;w.panels[i].pixels.size=1; }
    e.waterFlag=2;s.timer_ticks=1;
    assert(events_step(&e,&s,&a,&w)==0);
    assert(w.textures[0].pixels.data==panel_bytes+2 && e.waterFlag==0 && e.waterTimer==7);
    s.timer_ticks=7;
    assert(events_step(&e,&s,&a,&w)==0 && e.waterFlag==0);
    s.timer_ticks=8;
    assert(events_step(&e,&s,&a,&w)==0 && e.waterFlag==1);
    w.scene=7;w.prop_count=1;w.prop_objects[0]=2;w.prop_types[0]=160;
    e.prop_base_texture[0]=12;e.waterTimer=100;w.texture_count=20;
    assert(events_step(&e,&s,&a,&w)==0 && w.objects[2].texture==13);
}

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
    remaining_events();
    cosmetics();
    event_setup(0);snd_enable(0,0);e.autoDestruct=4;e.autoDTime=16800;e.autoMSlot=-1;
    e.ticks=2400;
    assert(events_countdown(&e,&s,&a,&w)==0 && e.autoDestruct==4);
    e.ticks=2401;
    assert(events_countdown(&e,&s,&a,&w)==0 && e.autoDestruct==5 && e.autoDMsgTimer==2561);
    e.ticks=16800;
    assert(events_countdown(&e,&s,&a,&w)==0 && !e.autoDestruct && e.pc_dead_man==1);
    return 0;
}
