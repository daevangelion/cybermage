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

typedef struct { FILE *out; uint64_t sequence; int64_t frame; } rng_trace;

static void rng_observe(void *context, const char *file, unsigned line,
                         int bounded, int32_t bound, int32_t value,
                         const uint16_t before[3], const uint16_t after[3])
{
    rng_trace *t = context;
    fprintf(t->out, "%" PRIu64 ",%" PRId64 ",%s,%s:%u,%" PRId32
            ",%u,%u,%u,%" PRId32 ",%u,%u,%u\n", t->sequence++, t->frame,
            bounded ? "RND" : "RANDOM", file, line, bound,
            before[0], before[1], before[2], value, after[0], after[1], after[2]);
}

int main(int argc, char **argv)
{
    cm_replay replay = {0};
    cm_replay_frame f;
    cm_input input = {0};
    unsigned id;
    unsigned long seed;
    char *end;
    rng_trace rng = {NULL, 0, -1};
    int next, initialized = 0;
    if ((argc != 4 && argc != 5) || !*argv[3] || *argv[3] == '-') return 2;
    seed = strtoul(argv[3], &end, 10);
    if (*end || seed > UINT16_MAX) return 2;
    world_rng_init(&w.rng, (uint16_t)seed);
    if (argc == 5) {
        rng.out = fopen(argv[4], "w");
        if (!rng.out) return 1;
        fputs("sequence,frame,operation,caller,bound,rng0,rng1,rng2,value,after0,after1,after2\n", rng.out);
        w.rng.observe = rng_observe; w.rng.observe_context = &rng;
    }
    if (replay_open(&replay, argv[2])) return 1;
    puts("frame,ticks,rng0,rng1,rng2,person,object,x,y,z,heading,state,status,hp,target,animation,cursor,active");
    while ((next = replay_next(&replay, &f)) > 0) {
        if (!initialized) {
            char path[CM_WORLD_PATH_MAX];
            cm_file rules_file = {0};
            cm_exe exe;
            int length = snprintf(path, sizeof(path), "%s/CM_HI.EXE", argv[1]);
            if (length < 0 || (size_t)length >= sizeof(path)) return 1;
            if (platform_file_read(&rules_file, path) ||
                cm_exe_init(&exe, &rules_file, path)) return 1;
            sim_init(&s, (uint32_t)f.values[CM_TRACE_PREVIOUS]);
            /* Reproduce the two real InitPC lifecycle calls: boot and New. */
            if (ai_init_pc(&e, &s, &w.rng, &exe, (unsigned)f.values[CM_TRACE_SCENE], 0) ||
                ai_init_pc(&e, &s, &w.rng, &exe, (unsigned)f.values[CM_TRACE_SCENE],
                            (uint32_t)f.values[CM_TRACE_PREVIOUS])) return 1;
            platform_file_release(&rules_file);
            if (world_load_scene(&w, argv[1], (unsigned)f.values[CM_TRACE_SCENE]) ||
                ai_init(&a, &w) || ai_load_tables(&a, argv[1]) ||
                world_finalize_scene(&w) || events_init(&e, &w)) return 1;
            ai_restore_pc_info(&e, &w);
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
        rng.frame = f.values[CM_TRACE_FRAME];
        replay_input(&s, &f);
        if (sim_frame(&s, &w, &input, (uint32_t)f.values[CM_TRACE_TICKS]) ||
            ai_update_objects(&a, &s, &w, (uint32_t)f.values[CM_TRACE_TICKS])) return 1;
    }
    replay_close(&replay); world_close(&w);
    if (rng.out && fclose(rng.out)) return 1;
    return next < 0;
}
