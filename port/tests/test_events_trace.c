#define main events_trace_main
#include "events_trace.c"
#undef main
#include <assert.h>

static ui_save_state encoded;

static FILE *csv_file(const char *text)
{
    FILE *file=tmpfile();
    assert(file && fputs(text,file)>=0);
    rewind(file);
    return file;
}

static void bytes(void)
{
    unsigned i,j,actual;
    uint32_t at;
    memset(&e,0,sizeof e);
    for(i=0;i<EVENTS_ITEM_MAX;++i) {
        events_item *v=&e.items[i];
        for(j=0;j<20;++j) v->name[j]=(char)(i+j+0x80);
        v->type=(int16_t)(i+1);v->object=-1;
        v->icon=INT32_MIN+(int32_t)(0x07654300u+i);v->quantity=-(int32_t)(i+3);
        v->kind=(uint8_t)i;v->subtype=(uint8_t)(i+1);
        v->flags=0xa5;v->variant=(uint8_t)(0x80+i);v->unk_0024=-(int32_t)(i+80);
    }
    e.cash=-456;e.spell=7;e.hh_weapon=3;e.weapon=9;
    e.item_spell=5;e.weapon_slot=17;e.hh_dice=0xfedcba98;
    for(i=0;i<8;++i) e.spells[i]=(uint8_t)(0x80+i);
    for(i=0;i<6;++i) {e.armor[i]=(int16_t)(-20-(int)i);e.max_armor[i]=(int16_t)(100+i);}
    for(i=0;i<12;++i) {e.ammo[i]=(int16_t)(-30-(int)i);e.max_ammo[i]=(int16_t)(200+i);}
    e.energy=0x1234;e.max_energy=0x4321;
    a.pc.regen_interval=61;
    assert(!events_save_export(&e,&encoded));
    ai_save_export(&a,&encoded);
    for(at=0x130D64;at<0x131418;++at) {
        unsigned offset=at-0x130D50;
        unsigned expected=encoded.unk_130D50[offset/2]>>(offset%2*8)&255;
        assert(inventory_byte(at,&actual) && actual==expected);
    }
    e.selectors[EVENTS_SELECT_KEY-1].count=20;
    e.selectors[EVENTS_SELECT_KEY-1].index=-1;
    for(i=0;i<20;++i) e.selector_keys[i]=(int32_t)i-10;
    for(i=0;i<88;++i) {
        uint32_t word=i<4?20u:i<8?UINT32_MAX:(uint32_t)e.selector_keys[(i-8)/4];
        assert(inventory_byte(0x144974+i,&actual) && actual==(word>>(i%4*8)&255));
    }
    actual=123;
    for(at=0x131418;at<0x131424;++at) assert(!inventory_byte(at,&actual) && actual==123);
}

static void inputs(void)
{
    item_input item;
    selector_input selector;
    player_input player;
    player_reader reader={0};
    FILE *file=csv_file("4294967295,4294967295,6,2,1\n");
    assert(next_item_input(file,&item,"item-test")==1);
    assert(item.frame==UINT32_MAX && item.ticks==UINT32_MAX && item.type==6);
    assert(!next_item_input(file,&item,"item-test"));fclose(file);
    {
        static const char *bad[]={
            "-1,2,6,2,1\n","0,4294967296,6,2,1\n","0,2,0,2,1\n",
            "0,2,6,2842,1\n","0,2,6,2,1,7\n","0,2,6,2,1"
        };
        unsigned i;
        for(i=0;i<sizeof bad/sizeof bad[0];++i) {
            file=csv_file(bad[i]);
            assert(next_item_input(file,&item,"item-test")<0);fclose(file);
        }
    }
    reader.file=csv_file(
        "0,10,1,2,3,0,1,0,0,0,0,0,0,17,-1,0,0,1\n"
        "0,10,1,2,3,1,2,0,0,0,0,0,0,-4,-1,0,0,1\n"
        "1,11,1,2,3,0,1,0,0,0,0,0,0,40,-1,0,0,1\n");
    assert(next_player_input(&reader,&player,"actor-test")==1 && !player.frame && player.hp==17);
    assert(next_player_input(&reader,&player,"actor-test")==1 && player.frame==1 && player.hp==40);
    assert(!next_player_input(&reader,&player,"actor-test"));fclose(reader.file);
    memset(&reader,0,sizeof reader);
    reader.file=csv_file("0,10,1,2,3,1,2,0,0,0,0,0,0,17,-1,0,0,1\n");
    assert(next_player_input(&reader,&player,"actor-test")<0);fclose(reader.file);
    memset(&reader,0,sizeof reader);
    reader.file=csv_file("0,10,1,2,3,0,1,0,0,0,0,0,0,32768,-1,0,0,1\n");
    assert(next_player_input(&reader,&player,"actor-test")<0);fclose(reader.file);
    file=csv_file("0,10,0,1,0,-5,17\n");
    assert(next_selector_input(file,&selector,"selector-test")==1);
    memset(&s,0,sizeof s);s.kb_scan_status[42]=12;
    assert(apply_selector_input(&selector,"selector-test")<0);
    s.input_device=1;
    assert(!apply_selector_input(&selector,"selector-test"));
    assert(s.mouse_hand_x==-5 && s.mouse_hand_y==17 && s.kb_scan_status[42]==12);
    assert(!next_selector_input(file,&selector,"selector-test"));fclose(file);
    file=csv_file("0,10,0,4294967295,-1,-5,17\n");
    assert(next_selector_input(file,&selector,"selector-test")==1);
    assert(selector.mouse==1 && selector.joystick==1);fclose(file);
    file=csv_file("0,10,1,0,0,0,0\n");
    assert(next_selector_input(file,&selector,"selector-test")==1);
    assert(!apply_selector_input(&selector,"selector-test"));
    assert((uint32_t)e.selectors[0].index==UINT32_MAX &&
           (uint32_t)e.selectors[1].index==UINT32_MAX &&
           (uint32_t)e.selectors[2].index==UINT32_MAX);fclose(file);
    assert(!e.selectors[0].count && !e.selectors[1].count && !e.selectors[2].count);
    assert(e.selector_keys[0]==-10 && e.selector_keys[19]==9);
    {
        static const char *bad[]={
            "0,10,3,0,0,0,0\n",
            "0,10,1,0,0,1,0\n","0,10,0,1,0,2147483647,0\n",
            "0,10,0,0,1,1,0\n"
        };
        unsigned i;
        for(i=0;i<sizeof bad/sizeof bad[0];++i) {
            file=csv_file(bad[i]);
            assert(next_selector_input(file,&selector,"selector-test")<0);fclose(file);
        }
    }
}

static void ranges(void)
{
    cm_replay replay={0};
    replay.range_count=1;
    replay.ranges[0].address=0x206820;replay.ranges[0].length=24;
    assert(regen_timer_range(&replay));
    replay.ranges[0].length=8;assert(!regen_timer_range(&replay));
    replay.ranges[0].length=24;replay.ranges[0].relative=1;
    assert(!regen_timer_range(&replay));
}

static void pickup(void)
{
    item_input item={0,10,6,2,1};
    memset(&w,0,sizeof w);memset(&e,0,sizeof e);memset(&s,0,sizeof s);
    w.object_count=3;w.person_count=1;w.player_object=1;
    w.persons[0].object=1;w.persons[0].unk_0038=40*65536+17;
    w.top_level=1;w.unk_130D20=0x1ffff;
    w.objects[1].active=w.objects[2].active=1;
    w.objects[1].targetable=1;w.objects[2].targetable=2;w.objects[2].kind=2;
    w.objects[1].x=w.objects[1].y=w.objects[2].x=w.objects[2].y=65536;
    w.objects[2].radius=w.objects[2].z=w.objects[2].height=0x1000;
    w.prop_count=1;w.prop_objects[0]=2;w.prop_items[0]=6;
    e.definition_count=6;e.definitions[6].kind=9;e.definitions[6].dice_add=1;
    assert(!world_make_omap(&w));
    events_pickups_begin(&e);
    item.type=5;assert(apply_item_input(&item,"item-test")<0);
    assert(!e.items[0].type && w.objects[2].active);
    item.type=6;item.source=2;assert(apply_item_input(&item,"item-test")<0);
    assert(!e.items[0].type && w.objects[2].active);
    item.source=1;assert(!apply_item_input(&item,"item-test"));
    assert(e.items[0].type==6 && e.items[0].quantity==1 && w.objects[2].active);
    assert(!apply_item_input(&item,"item-test") && e.items[0].quantity==1);
    assert(!events_pickups_end(&e,&w) && !w.objects[2].active);
    assert((uint16_t)w.persons[0].unk_0038==17);
    assert(!events_use_item(&e,&s,&w,0));
    assert(!e.items[0].type && (uint16_t)w.persons[0].unk_0038==40);
}

int main(void)
{
    bytes();inputs();ranges();pickup();
    puts("event verifier: 1716 inventory/status bytes, 88 selector bytes and hostile input/pickup controls passed");
    return 0;
}
