#include "../src/sim/sim_internal.h"
#include "ai.h"
#include "audio.h"
#include "events.h"
#include <assert.h>
#include <string.h>

static world w;
static events_state e;
static sim_state s;
static ai_state a;

static void jumble_axis_proposals(void)
{
    static const struct {
        int32_t x, y, dx, dy, want_x, want_y;
        uint16_t mask;
    } cases[] = {
        {0x1c100,0x18000,0,-1024,0x1bfff,0x17c00,0x0800},
        {0x18000,0x1c100,-1024,0,0x17c00,0x1bfff,0x4000},
        {0x10100,0x18000,0,-1024,0x14000,0x17c00,0x0100},
        {0x18000,0x10100,-1024,0,0x17c00,0x14000,0x0004},
        {0x1c100,0x18000,512,-1024,0x1c300,0x17c00,0}
    };
    unsigned i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        world_object *o;
        memset(&w, 0, sizeof w); memset(&s, 0, sizeof s);
        w.object_count = 2; w.person_count = 1; w.player_object = 1;
        w.persons[0].object = 1;
        o = &w.objects[1];
        o->person = 0; o->x = cases[i].x; o->y = cases[i].y;
        o->active = o->targetable = 1;
        w.path_mask[0][33] = cases[i].mask;
        assert(!world_make_omap(&w));
        /* JumbleBound046C87..046CBC writes a proposal axis only for
         * origin quarter0/3. Middle quarters retain that axis's delta. */
        assert(!sim_jumble_bound(&s, &w, 1, cases[i].dx, cases[i].dy, 1));
        assert(o->x == cases[i].want_x && o->y == cases[i].want_y);
    }
}

static void crouch_contact(void)
{
    world_rng expected;
    world_object *player, *ceiling;
    memset(&w, 0, sizeof w);
    memset(&s, 0, sizeof s);
    memset(&a, 0, sizeof a);
    w.object_count = 3; w.person_count = 1; w.player_object = 1;
    w.persons[0].object = 1;
    player = &w.objects[1]; ceiling = &w.objects[2];
    player->x = player->y = 0x18000;
    player->z = 0x6000;
    ceiling->x = ceiling->y = 0x18000;
    ceiling->radius = ceiling->height = 0x4000;
    ceiling->z = 0x8000; ceiling->flags = 0x00800000;
    w.flatmap.nodes[33].object = 2;
    s.body_height = player->z + player->z / 2 + 0x1000;
    s.ai = &a; a.monster_count = 1; a.pc_sound = -1;
    snd_enable(0, 0);
    world_rng_init(&w.rng, 44);
    expected = w.rng;
    (void)world_rnd(&expected, 0x4000);
    /* MovePerson047A9F..047AFA: forced crouch checks sample0, consumes its
     * actual volume draw even muted, and raises the next frame's hit flash. */
    assert(!sim_move_person(&s, &w, 1, 0, 0, 0));
    assert(s.unk_144b34 == 0x8000 - 0x1100 - 0xa000);
    assert(a.pc_flash == 2 && a.pc_sound == -1);
    assert(!memcmp(w.rng.word, expected.word, sizeof expected.word));
    /* No new adjustment means no sound/RNG/flash write. */
    a.pc_flash = 0;
    assert(!sim_move_person(&s, &w, 1, 0, 0, 0));
    assert(!a.pc_flash);
    assert(!memcmp(w.rng.word, expected.word, sizeof expected.word));
    /* Existing stronger hit feedback is not lowered by a ceiling contact. */
    s.unk_144b34 = 0; s.body_height = 0xa000; a.pc_flash = 5;
    (void)world_rnd(&expected, 0x4000);
    assert(!sim_move_person(&s, &w, 1, 0, 0, 0));
    assert(a.pc_flash == 5);
    assert(!memcmp(w.rng.word, expected.word, sizeof expected.word));
}

int main(void)
{
    int32_t x = 1000124, y = 884972;
    world_object *actor = &w.objects[970], *cash = &w.objects[877];
    /* Exact matched CollObject entry at combat-actors frame20: node431 holds
     * object877. Original steering sees flags0; using object431 sets bit2. */
    world_trig_init(&w.trig);
    w.object_count = 971;
    w.player_object = 936;
    w.prop_count = 1;
    w.prop_objects[0] = 877;
    w.prop_items[0] = 17;
    actor->x = 1000027; actor->y = 884736;
    actor->z = 12288; actor->height = 19456; actor->unk_74AF0 = 7680;
    cash->x = 1015808; cash->y = 884736; cash->z = cash->height = 6400;
    cash->unk_74AF0 = 7680; cash->targetable = 2; cash->flags = 0x60080800;
    cash->active = 1;
    w.omap.nodes[431].object = 877;
    w.objects[431].x = 1000200; w.objects[431].y = 885000;
    w.objects[431].unk_74AF0 = 7680;
    e.definition_count = 17;
    e.definitions[17].kind = 15;
    s.events = &e;
    assert(!sim_coll_object(&s, &w, 970, &x, &y));
    assert(s.collision_flags == 0);
    e.definitions[17].kind = 16;
    x = 1000124; y = 884972;
    assert(!sim_coll_object(&s, &w, 970, &x, &y));
    assert(!s.collision_flags && x == 1000124 && y == 884972);
    /* Decoration has no item definition: player must not call CollItem(0). */
    w.prop_items[0] = 0;
    w.player_object = 970;
    x = 1000124; y = 884972;
    assert(!sim_coll_object(&s, &w, 970, &x, &y));
    /* Combat frame794: decoration918, no pickup definition. The generic
     * branch's [esp+8] follows a push and therefore names the object, not node. */
    memset(&w, 0, sizeof w);
    memset(&s, 0, sizeof s);
    world_trig_init(&w.trig);
    w.object_count = 971;
    w.player_object = 970;
    w.prop_count = 1;
    w.prop_objects[0] = 918;
    actor = &w.objects[970];
    cash = &w.objects[918];
    actor->x = 924488; actor->y = 979196; actor->floor_z = 131072;
    s.body_height = 39935;
    cash->x = 944128; cash->y = 984064; cash->floor_z = 131072;
    cash->z = cash->height = 32768; cash->unk_74AF0 = 7680;
    cash->flags = 0x60080800; cash->targetable = 2;
    w.omap.nodes[14 + 14 * CM_MAP_SIDE].object = 918;
    x = 933647; y = 980211;
    assert(!sim_coll_object(&s, &w, 970, &x, &y));
    assert(x == 944128 - 7680 - SIM_OBJECT_MARGIN - 1);
    crouch_contact();
    jumble_axis_proposals();
    return 0;
}
