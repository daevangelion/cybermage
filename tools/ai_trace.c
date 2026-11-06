#include "ai.h"
#include "events.h"
#include "replay.h"
#include "target.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static world w;
static ai_state a;
static sim_state s;
static events_state e;
static const vis_view view = {120, 90, 400, 300}; /* Original default ConWinInfo. */
static FILE *steering_file;
static uint64_t steering_sequence;
static int64_t observed_frame = -1;

typedef struct { FILE *out; unsigned object; uint64_t sequence; } collision_trace;

static void collision_row(collision_trace *t, const world *scene, unsigned object,
                           unsigned node, unsigned candidate, int32_t x, int32_t y)
{
    const world_object *a = &scene->objects[object], *b = &scene->objects[candidate];
    fprintf(t->out, "%" PRIu64 ",%" PRId64 ",%u,%" PRId32 ",%" PRId32
            ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32
            ",%" PRId32 ",%u,%u,%u,%" PRIu32 ",%" PRId32 ",%" PRId32
            ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%u\n",
            t->sequence, observed_frame, object, x, y, a->x, a->y, a->floor_z,
            a->z, a->height, (int32_t)a->unk_74AF0, node, candidate, b->targetable,
            b->flags, b->x, b->y, b->floor_z, b->z, b->height, (int32_t)b->unk_74AF0, b->active);
}

static void collision_observe(void *context, const world *scene, unsigned object,
                               int32_t x, int32_t y)
{
    collision_trace *t = context;
    int tx[2] = {(x - 0x4000) >> 16, (x + 0x4000) >> 16};
    int ty[2] = {(y - 0x4000) >> 16, (y + 0x4000) >> 16};
    unsigned row, col;
    if (object != t->object) return;
    collision_row(t, scene, object, 0, 0, x, y);
    for (row = 0; row < 2; ++row) {
        if (row && ty[0] == ty[1]) continue;
        for (col = 0; col < 2; ++col) {
            unsigned node, count = 0;
            if (col && tx[0] == tx[1]) continue;
            if (tx[col] < 0 || tx[col] >= CM_MAP_SIDE || ty[row] < 0 || ty[row] >= CM_MAP_SIDE) continue;
            node = (unsigned)(ty[row] * CM_MAP_SIDE + tx[col]);
            while (node && node < CM_CHAIN_NODES && ++count <= CM_CHAIN_NODES) {
                unsigned id = scene->omap.nodes[node].object;
                if (id < scene->object_count) collision_row(t, scene, object, node, id, x, y);
                node = scene->omap.nodes[node].next;
            }
        }
    }
    ++t->sequence;
}

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
    int result;
    if (platform_file_read(&file, path)) return -1;
    result = ai_import_person_undefined(&w, file.data, file.size);
    platform_file_release(&file);
    if (result) return result;
    fprintf(stderr, "VERIFICATION ONLY: imported PERSON+0x24 for %u actors from %s; no other fields imported\n",
            w.person_count, path);
    return 0;
}

static int actor_clock(FILE *file, const char *path, uint32_t frame,
                        uint32_t *normal, uint32_t *fast)
{
    char line[64], *cursor = line, *end;
    unsigned long value[3];
    unsigned i;
    if (!fgets(line, sizeof line, file))
        return ferror(file) ? cm_fail(path, frame, "reading actor clocks") : 0;
    for (i = 0; i < 3; ++i) {
        if (*cursor < '0' || *cursor > '9')
            return cm_fail(path, frame, "invalid actor clock integer");
        errno = 0;
        value[i] = strtoul(cursor, &end, 10);
        if (errno || value[i] > UINT32_MAX ||
            (i < 2 ? *end != ',' : (*end != '\n' || end[1])))
            return cm_fail(path, frame, "invalid actor clock row");
        cursor = end + 1;
    }
    if (value[0] != frame) return cm_fail(path, frame, "actor clock frame mismatch");
    *fast = (uint32_t)value[1]; *normal = (uint32_t)value[2];
    return 1;
}

int main(int argc, char **argv)
{
    cm_replay replay = {0};
    cm_replay_frame f;
    cm_input input = {0};
    unsigned id;
    unsigned long seed;
    char *end;
    const char *rng_path = NULL, *undefined_path = NULL, *steering_path = NULL, *collision_path = NULL;
    const char *clock_path = NULL;
    FILE *clocks = NULL;
    static rng_trace rng = {NULL, 0, -1};
    static collision_trace collision = {NULL, CM_OBJECT_MAX, 0};
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
        else if (!strcmp(argv[arg], "--collision-trace") && arg + 1 < argc && !collision_path)
            collision_path = argv[++arg];
        else if (!strcmp(argv[arg], "--actor-clocks") && arg + 1 < argc && !clock_path)
            clock_path = argv[++arg];
        else if (!strcmp(argv[arg], "--collision-object") && arg + 1 < argc) {
            unsigned long object = strtoul(argv[++arg], &end, 10);
            if (!*argv[arg] || *end || object >= CM_OBJECT_MAX) return 2;
            collision.object = (unsigned)object;
        }
        else return 2;
    }
    seed = strtoul(argv[3], &end, 10);
    if (*end || seed > UINT16_MAX) return 2;
    if (clock_path) {
        char header[64];
        clocks = fopen(clock_path, "r");
        if (!clocks) return cm_fail(clock_path, 0, "opening actor clocks") != 0;
        if (!fgets(header, sizeof header, clocks) ||
            strcmp(header, "frame,fast_ticks,normal_ticks\n"))
            return cm_fail(clock_path, 0, "invalid actor clock header") != 0;
        fprintf(stderr, "External IRQ0 actor clock inputs: %s\n", clock_path);
    }
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
    if (collision_path) {
        if (collision.object == CM_OBJECT_MAX) return 2;
        collision.out = fopen(collision_path, "w");
        if (!collision.out) return 1;
        fputs("sequence,frame,object,proposed_x,proposed_y,source_x,source_y,source_floor,source_z,source_height,source_radius,node,candidate,targetable,flags,x,y,floor,z,height,radius,active\n", collision.out);
    }
    if (replay_open(&replay, argv[2])) return 1;
    puts("frame,ticks,rng0,rng1,rng2,person,object,x,y,z,heading,state,status,hp,target,animation,cursor,active");
    while ((next = replay_next(&replay, &f)) > 0) {
        uint32_t normal_ticks = (uint32_t)f.values[CM_TRACE_TICKS], fast_ticks = normal_ticks;
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
                world_finalize_scene(&w) || sim_start_scene(&s, &w) ||
                events_init(&e, &w) || events_load_tables(&e, argv[1])) return 1;
            ai_restore_pc_info(&e, &w);
            ai_start_clock(&a, (uint32_t)f.values[CM_TRACE_PREVIOUS]);
            if (undefined_path && import_undefined(undefined_path)) return 1;
            s.ai = &a; s.events = &e; a.events = &e;
            e.actors = &a;
            if (collision.out) { s.collision_observe = collision_observe; s.collision_context = &collision; }
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
        if (clocks) {
            int result = actor_clock(clocks, clock_path, (uint32_t)observed_frame,
                                       &normal_ticks, &fast_ticks);
            if (result < 0) return 1;
            if (!result) {
                fprintf(stderr, "No completed UpdateObjects clock pair for terminal frame %" PRId64 "\n",
                        observed_frame);
                if (replay_next(&replay, &f) != 0)
                    return cm_fail(clock_path, (uint32_t)observed_frame, "actor clocks end before final frame") != 0;
                break;
            }
        }
        replay_input(&s, &f);
        if (sim_frame(&s, &w, &input, (uint32_t)f.values[CM_TRACE_TICKS]) ||
            ai_pc_input(&a, &s, &w) || events_step(&e, &s, &a, &w) ||
            ai_update_objects(&a, &s, &w, normal_ticks, fast_ticks) ||
            ai_pc_hand2hand(&a, &s, &w)) return 1;
        {
            const world_object *pc = &w.objects[w.player_object];
            w.camera.u = pc->x; w.camera.v = pc->y;
            w.camera.z = pc->floor_z + s.body_height - 0x1000 + s.unk_144b38 + a.pc.hit_camera_z;
            w.camera.angle = pc->heading;
        }
        if (target_query(&a.targets, &w, &w.camera, &view)) return 1;
        ai_pc_draw_advance(&a, &s);
    }
    replay_close(&replay); world_close(&w);
    if (clocks) {
        if (fgetc(clocks) != EOF || ferror(clocks))
            return cm_fail(clock_path, 0, "unconsumed actor clocks") != 0;
        if (fclose(clocks)) return 1;
    }
    if (rng.out && fclose(rng.out)) return 1;
    if (steering_file && fclose(steering_file)) return 1;
    if (collision.out && fclose(collision.out)) return 1;
    return next < 0;
}
