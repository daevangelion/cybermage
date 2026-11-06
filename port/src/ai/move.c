#include "ai.h"
#include "audio.h"

/* MoveObject 0x33ED0 and DragonFly 0x3457C, CM_HI.EXE. */
static int32_t add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

static int32_t mul(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

int ai_move_object(ai_state *a, sim_state *s, world *w, unsigned person,
                   int tick, int moving)
{
    world_person *p;
    world_object *o;
    const ai_monster *m;
    const ai_sequence *seq;
    int sequence, frame;
    int32_t bob = 0;
    if (person >= w->person_count || ai_person_kind(a, w, person) < 0)
        return cm_fail("MoveObject", person, "invalid person");
    p = &w->persons[person]; m = &a->monsters[p->type];
    if (m->gsx < 0 || (unsigned)m->gsx >= a->gsx_count)
        return cm_fail("MoveObject", person, "invalid GSX");
    sequence = a->gsx_sequence[m->gsx];
    if (sequence < 0 || (unsigned)sequence >= a->sequence_count)
        return cm_fail("MoveObject", person, "invalid ANIMOBJ");
    seq = &a->sequences[sequence]; o = &w->objects[p->object];
    world_omap_delete(w, (unsigned)p->object);
    if (p->unk_0060) {
        if (tick) {
            int rising = p->unk_0068 != 0;
            p->unk_0064 = add(p->unk_0064, rising ? 0x200 : -0x200);
            if ((rising && p->unk_0064 > 0) || (!rising && p->unk_0064 < 0))
                p->unk_0068 = !rising;
        }
        bob = add(p->unk_0060, p->unk_0064);
    }
    if (p->unk_00BC >= AI_ANIM_STATES || p->unk_00BD >= AI_ANIM_STEPS)
        return cm_fail("MoveObject", person, "invalid animation cursor");
    if (tick || p->unk_0016) {
        unsigned cursor;
        p->unk_0018 = 0;
        if (p->unk_0016) {
            cursor = seq->start[p->unk_00BC];
            frame = seq->step[cursor]; p->unk_0016 = 0;
        } else {
            cursor = (unsigned)p->unk_00BD + 1;
            if (cursor >= AI_ANIM_STEPS)
                return cm_fail("MoveObject", person, "unterminated animation sequence");
            frame = seq->step[cursor];
            if (frame == 255) {
                cursor = seq->start[p->unk_00BC];
                frame = seq->step[cursor]; p->unk_0018 = 1;
            }
        }
        if (frame >= AI_ANIM_FRAMES)
            return cm_fail("MoveObject", person, "animation starts at terminator");
        p->unk_00BD = (uint8_t)cursor;
        o->radius = seq->radius[frame]; o->height = seq->height[frame];
        o->unk_6F220 = seq->unk_011C[frame]; o->unk_71E88 = seq->unk_016C[frame];
        if (world_anim_frame(w, (unsigned)p->object, (unsigned)m->gsx,
                              seq->image[frame])) return -1;
        if (m->kind == 10 && (seq->image[frame] == 3 || seq->image[frame] == 0))
            snd_object(w, seq->image[frame] == 3 ? 111u : 112u,
                       (unsigned)p->object, 0x40000, 0x9FFF, 0);
    } else frame = seq->step[p->unk_00BD];
    if (frame >= AI_ANIM_FRAMES)
        return cm_fail("MoveObject", person, "animation frame outside table");
    if (p->unk_00A4) {
        if (person != w->player_person) {
            uint16_t heading = o->heading;
            o->heading = (uint16_t)p->unk_00B4;
            if (sim_move_monster(s, w, (unsigned)p->object,
                                 s->bp_limit_flag ? p->unk_00A8 / 128 : 2,
                                 add(p->unk_00B0, bob))) return -1;
            o->heading = heading;
            if (s->bp_limit_flag) {
                if (p->water) {
                    p->unk_00A8 /= 2; p->unk_00AC = 0; p->unk_00B0 = 0;
                } else p->unk_00A8 = mul(p->unk_00A8, 4) / 5;
                if (p->unk_00A4 > 0) {
                    --p->unk_00A4;
                    if (!p->unk_00A4 || (p->unk_00A8 < 0x4000 &&
                                         p->unk_00B0 / 2 > p->unk_00A8)) {
                        p->unk_00A4 = -1;
                        p->unk_00AC = w->unk_5E7A8 ? 0x4CC : 0x800;
                    } else {
                        p->unk_00AC = mul(p->unk_00AC, 2) / 3;
                        p->unk_00B0 = add(p->unk_00B0, p->unk_00AC);
                    }
                }
                if (p->unk_00A4 < 0) {
                    p->unk_00AC = mul(p->unk_00AC, 2);
                    p->unk_00B0 = add(p->unk_00B0, -p->unk_00AC);
                    if (p->unk_00B0 <= 0) { p->unk_00A4 = 0; p->unk_00B0 = 0; }
                }
            }
        }
    } else if (moving) {
        int32_t speed = seq->speed[frame];
        int32_t vertical = bob;
        if (tick) vertical = add(mul(seq->vertical[frame], 64), bob);
        else if (speed) speed = 2;
        if (sim_move_monster(s, w, (unsigned)p->object, speed, vertical)) return -1;
    } else if (person != w->player_person && m->kind != 14) {
        int32_t old = o->floor_z, floor;
        if (sim_coll_flat(w, (unsigned)p->object, o->x, o->y)) return -1;
        floor = o->floor_z;
        if (bob) {
            o->floor_z = add(floor, bob);
            if (sim_coll_top_flat(s, w, (unsigned)p->object,
                                  old > floor ? old : floor, 0x1100) < 0) return -1;
            if (floor > o->floor_z) o->floor_z = floor;
        }
        if (add(old, -0x4000) > o->floor_z)
            o->floor_z = add(old, -(int32_t)((int64_t)0x4000 * s->frame_ticks / 2));
    }
    world_omap_insert(w, (unsigned)p->object);
    return 0;
}

int ai_dragon_fly(ai_state *a, sim_state *s, world *w, unsigned person,
                  unsigned target)
{
    world_person *p = &w->persons[person];
    world_object *o = &w->objects[p->object];
    const ai_monster *m = &a->monsters[p->type];
    int32_t old = o->floor_z, floor, ceiling;
    int32_t roll, delta;
    if (sim_coll_flat(w, (unsigned)p->object, o->x, o->y)) return -1;
    floor = o->floor_z; o->floor_z = 0x3B7FF;
    if (sim_coll_top_flat(s, w, (unsigned)p->object,
                          old > floor ? old : floor, 0x1100) < 0) return -1;
    ceiling = o->floor_z; o->floor_z = old;
    if (target && p->state) {
        const world_object *t = &w->objects[target];
        if (p->unk_006C != 3 && p->unk_006C != 0)
            p->unk_006C = world_rnd(&w->rng, 6) ? 3 : 0;
        p->unk_0070 = add(t->floor_z, t->height);
        if (p->unk_006C != 3)
            p->unk_0070 = add(p->unk_0070, add(t->height / 2, 0x1000));
    } else {
        roll = world_rnd(&w->rng, 64);
        switch (roll) {
        case 7:
            p->unk_0070 = add(floor, p->unk_0060);
            p->unk_0070 = add(p->unk_0070,
                (int32_t)((int64_t)add(ceiling, -p->unk_0070) *
                          (world_rnd(&w->rng, 192) + 64) / 256));
            break;
        case 21: p->unk_006C = 0; break;
        case 32: p->unk_006C = 3; break;
        case 43: p->unk_006C = 1; break;
        case 59: p->unk_006C = 2; break;
        default: break;
        }
    }
    if (p->unk_0070) {
        if (ceiling < p->unk_0070) p->unk_0070 = ceiling;
        delta = add(p->unk_0070, -add(floor, p->unk_0060));
        if (delta > 0x1000) delta = 0x1000;
        else if (delta < -0x1000) delta = -0x1000;
        else p->unk_0070 = 0;
        p->unk_0060 = add(p->unk_0060, delta);
    }
    if (o->target_mode == 2) p->unk_006C = 0;
    if (p->unk_006C == 1 || p->unk_006C == 2) {
        o->target_mode = 1;
        o->target_angle = (uint16_t)(o->heading + (p->unk_006C == 1 ? 0x800 : -0x800));
    } else if (p->unk_006C == 3) p->unk_0070 = add(p->unk_0060, -0x400);
    if (p->unk_006C == 3) {
        if (p->unk_0060 < 0x4000) { p->unk_0060 = 0x4000; p->unk_006C = 0; }
    } else if (p->unk_0060 < m->unk_00D0) p->unk_0060 = m->unk_00D0;
    return 0;
}
