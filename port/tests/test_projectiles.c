#include "ai.h"
#include "events.h"
#include "audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ai_state a;
static sim_state s;
static events_state e;
static world w;

static void setup(void)
{
    unsigned i;
    memset(&w,0,sizeof w); memset(&a,0,sizeof a); memset(&s,0,sizeof s); memset(&e,0,sizeof e);
    assert(ai_load_tables(&a,"../orig/retail") == 0);
    w.object_count = 2500; w.person_count = 2; w.scene = 1; w.top_level = 3;
    w.player_object = 1; w.player_person = 0; a.events = &e; s.ai = &a; s.events = &e;
    a.pc_learn_spell = -1; a.monster_count = AI_MONSTER_MAX; a.difficulty = 1;
    s.body_height = 0x10000; s.pc_mode = 1;
    w.rng.word[0] = 3000; w.rng.word[1] = 1; w.rng.word[2] = 29999;
    world_trig_init(&w.trig);
    for (i = 1; i <= 3; ++i) {
        w.objects[i].x = 0x88000 + (i == 2 ? 0x1000 : 0);
        w.objects[i].y = 0x88000; w.objects[i].z = 0x1000;
        w.objects[i].height = 0x8000; w.objects[i].active = 1;
        w.objects[i].person = (int16_t)(i - 1); w.objects[i].targetable = i != 3;
    }
    w.persons[0].object = 1; w.persons[1].object = 2;
    w.persons[0].unk_0038 = w.persons[1].unk_0038 = (1000 << 16) | 1000;
    w.fxo_count = w.prop_count = 33;
    for (i = 0; i < 33; ++i) {
        w.fxo_objects[i] = (uint16_t)(10+i); w.objects[10+i].texture = (uint16_t)(100+i*32);
        w.objects[10+i].radius = 0x800; w.objects[10+i].height = 0x1000;
        w.objects[10+i].flags = UINT32_C(0x08000000);
        w.prop_objects[i] = (uint16_t)(50+i);
        w.objects[50+i].flags = UINT32_C(0x20080800);
        w.objects[50+i].radius = 0x1400; w.objects[50+i].height = 0x2500;
        w.objects[50+i].texture = (uint16_t)(500+i*4); w.objects[50+i].rotations = 3;
    }
    for (i = 0; i < AI_MISSILE_MAX; ++i) a.missiles[i].object = (int32_t)(2400+i);
    a.pc_aim.pistol_angle = 32; a.pc_aim.pistol_xoffset = 320;
    a.pc_aim.pistol_lr = 16; a.pc_aim.pistol_ud = 64;
    a.pc_aim.pistol_boffset = 20; a.pc_aim.pistol_falloffset = 20;
    a.pc_aim.pistol_fall = 1; a.pc_aim.grenade_aim_center = 20;
    a.pc_aim.auto_enable = 0; /* This fixture exercises manual screen aim. */
    world_make_omap(&w); snd_enable(0,0);
}

static void radial(void)
{
    int health, damage;
    setup();
    health = (int16_t)w.persons[1].unk_0038;
    assert(ai_radial_damage(&a,&s,&w,3,0,1,0,0,0) == 0);
    damage = health - (int16_t)w.persons[1].unk_0038;
    assert(damage >= 14 && damage <= 56);
    a.monsters[0].resistance[4] = 100;
    assert(ai_freeze_monster(&a,&s,&w,2,2,12) == 0 && !w.persons[1].unk_0058);
    a.monsters[0].resistance[4] = 0;
    assert(ai_freeze_monster(&a,&s,&w,2,2,12) == 0 && w.persons[1].unk_0058 >= 72);
    assert(ai_pc_zone_radius(&w,3,8*32+8) == 1);
    assert(ai_pc_zone_radius(&w,3,20*32+20) == 0);
    a.pc_learn_spell = 3;
    damage = ai_pc_radial_damage(&a,&s,&w,3,0,0,0,100,100);
    assert(damage >= 14 && damage <= 56 && (e.spells[0] & 8));
}

static void launches(void)
{
    unsigned i;
    setup();
    assert(ai_pc_launch_missile(&a,&s,&w,2,320,20,0) == 1);
    assert(a.missiles[0].active == 1 && a.missiles[0].source == 1);
    assert(a.missiles[0].ticks == 2 && a.missiles[0].vertical == 0);
    assert(w.objects[2400].x == w.objects[1].x + 0x2800);
    assert(w.objects[2400].y == w.objects[1].y && !w.objects[2400].active);
    /* SetMissile02DAD7 uses the PROP descriptor table, not placed FXO:
     * copying a door's FX bit creates a phantom oriented missile blocker. */
    assert(w.objects[2400].flags == UINT32_C(0x21080000));
    assert(w.objects[2400].radius == 0x1400 && w.objects[2400].height == 0x2500);
    assert(w.objects[2400].texture == 500 + a.missile_types[2].prop * 4);
    assert(w.objects[2400].rotations == 3);
    assert(ai_pc_launch_missile(&a,&s,&w,4,2,0,(int16_t)0x8000) == 1);
    assert(a.missiles[1].target == 2 && a.missiles[1].vertical == 4);
    assert(w.objects[2401].heading == 0 && w.objects[2401].x == w.objects[2].x + 0x1000);
    a.pc_aim.auto_enable = 1;
    assert(ai_pc_launch_missile(&a,&s,&w,2,320,20,0) == 1);
    assert(a.pc_aim.aim_y == a.pc_rules.center_aim_y[1]);
    assert(a.missiles[2].vertical == a.pc_aim.aim_y - 20);
    for (i = 0; i < AI_MISSILE_MAX; ++i) a.missiles[i].active = 1;
    assert(ai_pc_launch_missile(&a,&s,&w,2,320,20,0) == 0);
}

static void transitions(void)
{
    int hp;
    ai_missile *m;
    setup(); m = &a.missiles[0]; m->active = 1; m->type = 5; m->source = 1;
    m->ticks = 0; m->variant = 0; m->radius = 0; m->vertical = 6;
    w.objects[2400] = w.objects[3]; w.objects[2400].targetable = 0;
    assert(ai_update_missile(&a,&s,&w) == 0 && m->active == 2 && m->vertical == 5);
    hp = (int16_t)w.persons[1].unk_0038;
    assert(ai_update_missile(&a,&s,&w) == 0 && m->vertical == 4);
    assert((int16_t)w.persons[1].unk_0038 == hp);
    m->type = 3; m->target = 2; m->vertical = 4;
    assert(ai_update_missile(&a,&s,&w) == 0 && m->type == 4 && m->vertical == 3);
    assert((int16_t)w.persons[1].unk_0038 < hp);
}

int main(void)
{
    assert(platform_init(1) == 0);
    radial(); launches(); transitions();
    platform_shutdown();
    puts("projectiles: radial dice/resistance/freeze, PC muzzle/slots, one-shot impact damage passed");
    return 0;
}
