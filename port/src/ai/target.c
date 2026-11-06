#include "ai.h"
#include "render.h"

/* All addresses in docs/re/ai.md refer to CM_HI.EXE. */
int ai_person_kind(const ai_state *a, const world *w, unsigned person)
{
    int type;
    if (person >= w->person_count) return -1;
    type = w->persons[person].type;
    if (type < 0 || (unsigned)type >= a->monster_count) return -1;
    return a->monsters[type].kind;
}

int target_query(ai_target_list *out, const world *w, const world_camera *camera)
{
    render_visibility_list visible;
    unsigned i;
    out->count = 0;
    if (render_visibility(w, camera, &visible)) return -1;
    for (i = 0; i < visible.count; ++i) {
        const render_visible_prop *p = &visible.props[i];
        if (p->targetable == 1 && p->left <= AI_TARGET_COLUMN &&
            p->right >= AI_TARGET_COLUMN)
            out->objects[out->count++] = p->object;
    }
    return 0;
}

/* Xox::SetAutoTarget, 0x2F150: reverse draw-candidate order, not nearest. */
int32_t ai_set_auto_target(const ai_state *a, const world *w)
{
    unsigned n = a->targets.count;
    while (n) {
        unsigned object = a->targets.objects[--n];
        int person, type;
        if (object >= w->object_count) continue;
        person = w->objects[object].person;
        if (person < 0 || (unsigned)person >= w->person_count) continue;
        if (w->persons[person].status == 2) continue;
        type = w->persons[person].type;
        if (type >= 0 && (unsigned)type < a->monster_count &&
            a->monsters[type].rank[0] > 0)
            return (int32_t)object;
    }
    return -1;
}

void ai_make_missile_list(ai_state *a, const world *w)
{
    unsigned i;
    a->missile_count = 0;
    for (i = 1; i < w->object_count; ++i)
        if (w->objects[i].active && !(w->objects[i].flags & UINT32_C(0x03000400)))
            a->missile_objects[a->missile_count++] = (uint16_t)i;
}

void ai_track_target(world *w, unsigned object, unsigned other, int offset)
{
    world_object *o = &w->objects[object];
    const world_object *t = &w->objects[other];
    uint16_t angle = cm_atan64k(o->x, o->y, t->x, t->y);
    if (offset)
        angle = (uint16_t)(angle + ((uint16_t)(angle - t->heading) < 0x8000 ?
                                  0x1555 : -0x1555));
    o->target_angle = angle;
    o->target_mode = 2;
}

static int32_t add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

static int sight(sim_state *s, world *w, unsigned source, unsigned target,
                 int32_t *distance, int32_t range)
{
    const world_object *o = &w->objects[source], *t = &w->objects[target];
    int32_t d = cm_distance(o->x, o->y, t->x, t->y);
    int32_t delta, z, tz;
    uint16_t angle;
    if (d >= range) return 0;
    angle = cm_atan64k(o->x, o->y, t->x, t->y);
    delta = (int16_t)(angle - o->heading);
    if (delta <= -0x4000 || delta >= 0x4000) return 0;
    z = add(add(o->floor_z, o->z), o->height);
    tz = add(t->floor_z, target == w->player_object ? s->body_height :
                                                         add(t->z, t->height));
    if (world_ray_sight(w, source, target, d, z, tz, angle) != 1) return 0;
    *distance = d;
    return 1;
}

int ai_sight_person(const ai_state *a, sim_state *s, world *w,
                    unsigned source, unsigned target, int32_t range)
{
    world_person *p;
    const world_person *t;
    int32_t distance;
    (void)a;
    if (source >= w->person_count || target >= w->person_count)
        return cm_fail("SightPerson", source, "person index out of range");
    p = &w->persons[source]; t = &w->persons[target];
    p->target = -1;
    if (t->status == 2 || (range > 0 && t->unk_005C)) return 0;
    if (range <= 0) range = 0x70000;
    if (sight(s, w, (unsigned)p->object, (unsigned)t->object, &distance, range)) {
        p->target = (int32_t)target;
        p->distance = distance;
    }
    return 0;
}

int ai_find_target(const ai_state *a, sim_state *s, world *w,
                   unsigned source, int32_t range)
{
    world_person *p;
    const ai_monster *m;
    unsigned i;
    int32_t selected = -1;
    int priority = 1;
    if (source >= w->person_count || ai_person_kind(a, w, source) < 0)
        return cm_fail("FindTarget", source, "invalid source person");
    p = &w->persons[source]; m = &a->monsters[p->type];
    for (i = 0; i < w->person_count; ++i) {
        const world_person *t = &w->persons[i];
        unsigned faction;
        int rank;
        int32_t distance;
        if (i == source || t->status == 2 || t->state == 9 || t->state == 10 ||
            t->unk_005C) continue;
        if (i == w->player_person) faction = 0;
        else {
            if (ai_person_kind(a, w, i) < 0)
                return cm_fail("FindTarget", i, "invalid target monster");
            faction = a->monsters[t->type].faction;
        }
        if (faction >= AI_FACTIONS)
            return cm_fail("FindTarget", faction, "invalid target faction");
        rank = m->rank[faction];
        if (rank < priority) continue;
        if (sight(s, w, (unsigned)p->object, (unsigned)t->object, &distance, range)) {
            priority = rank;
            range = distance;
            selected = (int32_t)i;
        }
    }
    p->target = selected; p->distance = range;
    return 0;
}

void ai_update_pc_object(world *w, int tick)
{
    world_person *p = &w->persons[w->player_person];
    world_object *o = &w->objects[w->player_object];
    if (p->unk_005C) {
        o->flags |= UINT32_C(0x00020000);
        if (tick && --p->unk_005C == 0) o->flags &= ~UINT32_C(0x00020000);
    }
    if (tick && p->unk_0074) --p->unk_0074;
}

int ai_face_shooter(const ai_state *a, world *w, unsigned shooter, int32_t target)
{
    const world_object *s;
    world_object *t;
    world_person *p;
    if (target == (int32_t)w->player_object || target <= 0) return 0;
    if (shooter >= w->object_count || (unsigned)target >= w->object_count)
        return cm_fail("FaceShooter", (uint32_t)target, "invalid object");
    s = &w->objects[shooter]; t = &w->objects[target];
    if (t->targetable != 1) return 0;
    if (t->person < 0 || (unsigned)t->person >= w->person_count || s->person < 0 ||
        (unsigned)s->person >= w->person_count)
        return cm_fail("FaceShooter", (uint32_t)target, "invalid person");
    if (shooter != w->player_object) {
        int st = w->persons[s->person].type, tt = w->persons[t->person].type;
        if (st < 0 || tt < 0 || (unsigned)st >= a->monster_count ||
            (unsigned)tt >= a->monster_count)
            return cm_fail("FaceShooter", (uint32_t)target, "invalid monster");
        if (a->monsters[st].faction == a->monsters[tt].faction) return 0;
    }
    ai_track_target(w, (unsigned)target, shooter, 0);
    p = &w->persons[t->person];
    p->target = s->person;
    p->distance = cm_distance(t->x, t->y, s->x, s->y);
    return 0;
}
