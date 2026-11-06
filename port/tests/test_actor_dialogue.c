#include "ai.h"
#include "audio.h"
#include "events.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ai_state a;
static sim_state s;
static events_state e;
static world w;

/* Isolate state3's real UpdateObjects decision from movement and visibility. */
int __wrap_ai_move_object(ai_state *actors, sim_state *simulation, world *game,
                         unsigned person, int tick, int moving)
{
    (void)actors; (void)simulation; (void)game;
    (void)person; (void)tick; (void)moving;
    return 0;
}
void __wrap_ai_track_target(world *game, unsigned object, unsigned other, int offset)
{
    (void)game; (void)object; (void)other; (void)offset;
}
int __wrap_ai_find_target(const ai_state *actors, sim_state *simulation,
                         world *game, unsigned source, int32_t range)
{
    (void)actors; (void)simulation; (void)game; (void)source; (void)range;
    return 0;
}
int __wrap_events_cosmetics(events_state *events, sim_state *simulation,
                            ai_state *actors, world *game)
{
    (void)events; (void)simulation; (void)actors; (void)game;
    return 0;
}

static void dialogue_case(unsigned serial)
{
    memset(&a, 0, sizeof a); memset(&s, 0, sizeof s);
    memset(&e, 0, sizeof e); memset(&w, 0, sizeof w);
    w.scene = 1; w.top_level = 3; w.object_count = 4; w.person_count = 2;
    w.player_object = 1; w.persons[0].object = 1;
    w.objects[1].x = w.objects[1].y = 0x88000;
    w.objects[1].person = 0; w.objects[1].active = 1;
    w.persons[0].unk_0038 = 40;
    w.persons[1].object = 2; w.persons[1].state = 3;
    w.persons[1].target = -1; w.persons[1].unk_0024 = 1;
    w.persons[1].unk_004C = w.persons[1].unk_0050 = -1;
    w.objects[2].active = 1; w.objects[2].person = 1;
    w.objects[2].x = w.objects[2].y = 0x88000;
    a.tables_loaded = 1; a.monster_count = 1; a.events = &e;
    e.actors = &a; e.ambient_slot = -1;
    s.ai = &a; s.events = &e; s.pc_mode = 5;
    e.text_trigger = 1; e.text_object = 2; e.text_serial = serial;
    e.text_switch1 = 1; e.switch_count = 2;
    e.switches[1].fxo = 0; e.switches[1].textures[1] = 12;
    w.fxo_count = 1; w.fxo_objects[0] = 3; w.objects[3].texture = 10;
    s.kb_scan_status[0x2b] = 2;
    assert(events_input(&e, &s, &a, &w) == 0);
    assert(!s.kb_scan_status[0x2b] && e.text_skip_pending && e.text_trigger == 1);
    assert(events_move_world(&e, &s, &a, &w) == 0);
    assert(e.text_trigger == 1 && e.text_skip_pending);
    assert(e.switches[1].state == 0 && w.objects[3].texture == 10);
    assert(ai_update_objects(&a, &s, &w, 0, 0) == 0);
    assert(w.persons[1].state == 3);
    assert(events_text_finish(&e, &w) == 0);
    assert(!e.text_trigger && !e.text_skip_pending && !e.text_serial);
    assert(e.switches[1].state == 1 && w.objects[3].texture == 12);
    assert(w.persons[1].state == 3);
    assert(events_text_finish(&e, &w) == 0);
    assert(e.switches[1].state == 1 && w.objects[3].texture == 12);
    assert(ai_update_objects(&a, &s, &w, 0, 0) == 0);
    assert(w.persons[1].state == 2);
}

int main(void)
{
    assert(platform_init(1) == 0);
    snd_enable(0, 0);
    dialogue_case(1);
    dialogue_case(0); /* A full skip must not restart an unregistered message. */
    platform_shutdown();
    puts("actor dialogue: Backslash completes after actors, next frame releases state3");
    return 0;
}
