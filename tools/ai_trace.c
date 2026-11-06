#include "ai.h"
#include "events.h"
#include "replay.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static world w;
static ai_state a;
static sim_state s;
static events_state e;
static FILE *steering_file;
static uint64_t steering_sequence;
static int64_t observed_frame = -1;

int __real_ai_monster_blocked(ai_state *, sim_state *, world *, unsigned, unsigned);

/* Link-time observer around the real function; no substituted computation. */
int __wrap_ai_monster_blocked(ai_state *actors, sim_state *simulation, world *scene,
                              unsigned object, unsigned collisions)
{
    if (steering_file && object < scene->object_count &&
        simulation->collision_level >= 0 && simulation->collision_level < CM_MAP_LEVELS) {
        const world_object *o = &scene->objects[object];
        int32_t tile = (o->x >> 16) + (o->y >> 16) * CM_MAP_SIDE;
        if (tile >= 0 && tile < CM_MAP_CELLS) {
            uint16_t mask = simulation->collision_mode ?
                scene->inertia_mask[simulation->collision_level][tile] :
                scene->path_mask[simulation->collision_level][tile];
            fprintf(steering_file, "%" PRIu64 ",%" PRId64 ",%u,%" PRId32 ",%" PRId32
                    ",%u,%u,%u,%u,%u,%u\n", steering_sequence++, observed_frame, object,
                    o->x, o->y, o->heading, o->target_angle, o->target_mode, collisions,
                    simulation->collision_flags, mask);
        }
    }
    return __real_ai_monster_blocked(actors, simulation, scene, object, collisions);
}

typedef struct { FILE *out; uint64_t sequence; int64_t frame; } rng_trace;

static void rng_observe(void *context, const char *file, unsigned line,
                         int bounded, int32_t bound, int32_t value,
                         const uint16_t before[3], const uint16_t after[3])
{
    rng_trace *t = context;
    fprintf(t->out, "%" PRIu64 ",%" PRId64 ",%s,%s:%u,%" PRId32
            ",%u,%u,%u,%" PRId32 ",%u,%u,%u,%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n", t->sequence++, t->frame,
            bounded ? "RND" : "RANDOM", file, line, bound,
            before[0], before[1], before[2], value, after[0], after[1], after[2],
            a.timer_ticks, a.animation_deadline, a.fast_animation_deadline);
}

static int import_undefined(const char *path)
{
    cm_file file = {0};
    uint32_t count, i;
    if (platform_file_read(&file, path)) return -1;
    if (file.size < 12 || memcmp(file.data, "CMPERSON", 8)) {
        platform_file_release(&file);
        return cm_fail(path, 0, "invalid original PERSON snapshot header");
    }
    count = cm_u32(file.data + 8);
    if (count != w.person_count || count > CM_OBJECT_MAX || file.size != 12 + count * 200) {
        platform_file_release(&file);
        return cm_fail(path, 8, "snapshot actor count/size does not match loaded scene");
    }
    for (i = 0; i < count; ++i) {
        const uint8_t *record = file.data + 12 + i * 200;
        if (cm_u32(record) != i || cm_u16(record + 8) != (uint16_t)w.persons[i].object) {
            platform_file_release(&file);
            return cm_fail(path, 12 + i * 200, "snapshot actor identity mismatch");
        }
        w.persons[i].unk_0024 = (int32_t)cm_u32(record + 8 + 0x24);
    }
    platform_file_release(&file);
    fprintf(stderr, "VERIFICATION ONLY: imported PERSON+0x24 for %u actors from %s; no other fields imported\n",
            count, path);
    return 0;
}

int main(int argc, char **argv)
{
    cm_replay replay = {0};
    cm_replay_frame f;
    cm_input input = {0};
    unsigned id;
    unsigned long seed;
    char *end;
    const char *rng_path = NULL, *undefined_path = NULL, *steering_path = NULL;
    rng_trace rng = {NULL, 0, -1};
    int next, initialized = 0;
    int arg;
    if (argc < 4 || !*argv[3] || *argv[3] == '-') return 2;
    for (arg = 4; arg < argc; ++arg) {
        if (!strcmp(argv[arg], "--rng-trace") && arg + 1 < argc && !rng_path)
            rng_path = argv[++arg];
        else if (!strcmp(argv[arg], "--import-undefined") && arg + 1 < argc && !undefined_path)
            undefined_path = argv[++arg];
        else if (!strcmp(argv[arg], "--steering-trace") && arg + 1 < argc && !steering_path)
            steering_path = argv[++arg];
        else return 2;
    }
    seed = strtoul(argv[3], &end, 10);
    if (*end || seed > UINT16_MAX) return 2;
    world_rng_init(&w.rng, (uint16_t)seed);
    if (rng_path) {
        rng.out = fopen(rng_path, "w");
        if (!rng.out) return 1;
        fputs("sequence,frame,operation,caller,bound,rng0,rng1,rng2,value,after0,after1,after2,ticks,normal_deadline,fast_deadline\n", rng.out);
        w.rng.observe = rng_observe; w.rng.observe_context = &rng;
    }
    if (steering_path) {
        steering_file = fopen(steering_path, "w");
        if (!steering_file) return 1;
        fputs("sequence,frame,object,x,y,heading,target,mode,collisions,flags,mask\n", steering_file);
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
            ai_start_clock(&a, (uint32_t)f.values[CM_TRACE_PREVIOUS]);
            if (undefined_path && import_undefined(undefined_path)) return 1;
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
        observed_frame = rng.frame;
        replay_input(&s, &f);
        if (sim_frame(&s, &w, &input, (uint32_t)f.values[CM_TRACE_TICKS]) ||
            ai_update_objects(&a, &s, &w, (uint32_t)f.values[CM_TRACE_TICKS])) return 1;
    }
    replay_close(&replay); world_close(&w);
    if (rng.out && fclose(rng.out)) return 1;
    if (steering_file && fclose(steering_file)) return 1;
    return next < 0;
}
