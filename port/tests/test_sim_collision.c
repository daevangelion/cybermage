#include "../src/sim/sim_internal.h"
#include "events.h"
#include <assert.h>
#include <string.h>

static world w;
static events_state e;
static sim_state s;

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
    return 0;
}
