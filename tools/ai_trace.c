#include "ai.h"
#include "events.h"
#include "replay.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static world w;
static ai_state a;
static sim_state s;
static events_state e;

int main(int argc, char **argv)
{
    cm_replay replay = {0};
    cm_replay_frame f;
    cm_input input = {0};
    unsigned id;
    unsigned long seed;
    char *end;
    int next, initialized = 0;
    if (argc != 4 || !*argv[3] || *argv[3] == '-') return 2;
    seed = strtoul(argv[3], &end, 10);
    if (*end || seed > UINT16_MAX) return 2;
    world_rng_init(&w.rng, (uint16_t)seed);
    if (replay_open(&replay, argv[2])) return 1;
    puts("frame,ticks,rng0,rng1,rng2,person,object,x,y,z,heading,state,status,hp,target,animation,cursor,active");
    while ((next = replay_next(&replay, &f)) > 0) {
        if (!initialized) {
            if (world_load_scene(&w, argv[1], (unsigned)f.values[CM_TRACE_SCENE]) ||
                ai_init(&a, &w) || ai_load_tables(&a, argv[1]) ||
                world_finalize_scene(&w) || events_init(&e, &w)) return 1;
            sim_init(&s, (uint32_t)f.values[CM_TRACE_PREVIOUS]);
            s.ai = &a; s.events = &e; a.events = &e;
            s.body_height = w.objects[w.player_object].z +
                            w.objects[w.player_object].z / 2 + 0x1000;
            initialized = 1;
        }
        for (id = 0; id < w.person_count; ++id) {
            const world_person *p = &w.persons[id];
            const world_object *o = &w.objects[p->object];
            printf("%" PRId64 ",%" PRId64 ",%u,%u,%u,%u,%d,%" PRId32 ",%" PRId32
                   ",%" PRId32 ",%u,%d,%d,%d,%" PRId32 ",%u,%u,%u\n",
                   f.values[CM_TRACE_FRAME], f.values[CM_TRACE_TICKS],
                   w.rng.word[0], w.rng.word[1], w.rng.word[2], id, p->object,
                   o->x, o->y, o->floor_z, o->heading, p->state, p->status,
                   (int16_t)p->unk_0038, p->target, p->unk_00BC, p->unk_00BD, o->active);
        }
        replay_input(&s, &f);
        if (sim_frame(&s, &w, &input, (uint32_t)f.values[CM_TRACE_TICKS]) ||
            ai_update_objects(&a, &s, &w, (uint32_t)f.values[CM_TRACE_TICKS])) return 1;
    }
    replay_close(&replay); world_close(&w);
    return next < 0;
}
