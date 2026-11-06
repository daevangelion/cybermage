#include "ai.h"
#include "audio.h"
#include "events.h"

/* CM_HI.EXE UpdateObjects 0x348C4. State jump table is at 0x34888. */

static int32_t add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}
static void word(int32_t *p, int16_t value)
{
    *p = (int32_t)(((uint32_t)*p & UINT32_C(0xFFFF0000)) | (uint16_t)value);
}
static void anim(world_person *p, unsigned state)
{
    if (p->unk_00BC != state) { p->unk_00BC = (uint8_t)state; p->unk_0016 = 1; }
}
static void attack(world_person *p, int type)
{
    p->state = 5; p->unk_001A = (int16_t)type; p->unk_0034 = 0;
}
static int overlap(const world_object *a, const world_object *b)
{
    return add(add(b->floor_z, b->z), b->height) > a->floor_z &&
           add(add(a->floor_z, a->z), a->height) > b->floor_z;
}
static void sound(const world *w, const world_person *p, unsigned id, int32_t range)
{
    if (id) snd_object(w, id, (unsigned)p->object, range, 0x7FFF, 0);
}
static int backstep(sim_state *s, world *w, world_object *o, int amount)
{
    unsigned angle = (uint16_t)(o->heading + 0x8000) / 32;
    int32_t dx = (int32_t)((int64_t)amount * w->trig.cos[angle] >> 16);
    int32_t dy = (int32_t)((int64_t)amount * w->trig.sin[angle] >> 16);
    return sim_jumble_bound(s, w, (unsigned)(o - w->objects), dx, dy, 1);
}
static int airborne(ai_state *a, sim_state *s, world *w, unsigned id, unsigned target)
{
    int kind = ai_person_kind(a, w, id);
    if (s->bp_limit_flag && (kind == 10 || kind == 17 || (kind == 16 && w->scene == 10)))
        return ai_dragon_fly(a, s, w, id, target);
    return 0;
}

static int choose_attack(ai_state *a, world *w, world_person *p,
                          const ai_monster *m, const world_object *o,
                          unsigned target, int melee, int spells, int32_t distance)
{
    if (melee && distance < m->melee_range && overlap(o, &w->objects[target]) &&
        world_rnd(&w->rng, p->unk_001E) == 0) {
        ai_track_target(w, (unsigned)p->object, target, p->unk_0020 == 0);
        attack(p, 1); p->unk_0028 = 0; return 1;
    }
    if (p->unk_0028) return 0;
    if (p->unk_001E) {
        ai_track_target(w, (unsigned)p->object, target, p->unk_0020 == 0);
        return 1;
    }
    if (((m->attack[1] || spells) && distance < m->fire_range) ||
        (m->attack[3] && distance < 0x24000)) {
        ai_track_target(w, (unsigned)p->object, target, 0);
        p->unk_0016 = 1;
        p->unk_0028 = (int16_t)(m->attack[5] && world_rnd(&w->rng, 2) ? 4 : 3);
        if (m->kind != 6 && m->kind != 14) p->unk_001E = (int16_t)(1 + world_rnd(&w->rng, 4));
    } else ai_track_target(w, (unsigned)p->object, target, p->unk_0020 == 0);
    (void)a;
    return 1;
}

static int actor_attack(ai_state *a, sim_state *s, world *w, unsigned id,
                         int tick, int moving, int fire_moving, unsigned target,
                         const int32_t *distance, int can_move)
{
    world_person *p = &w->persons[id];
    const ai_monster *m = &a->monsters[p->type];
    world_object *o = &w->objects[p->object];
    p->unk_0078 = 0;
    if (!target && p->unk_002C >= 0 && (unsigned)p->unk_002C < w->person_count) {
        p->target = p->unk_002C; target = (unsigned)w->persons[p->target].object;
    }
    if (airborne(a, s, w, id, target)) return -1;
    if (target) ai_track_target(w, (unsigned)p->object, target, 0);
    if (p->unk_0028 > 2) p->unk_0028 -= 2;
    if (ai_move_object(a, s, w, id, tick, p->unk_001A == 1 ? fire_moving : moving)) return -1;
    if (tick && m->kind == 15) {
        if (a->fire_missile[m->fire] == 17) {
            unsigned pan;
            int vol;
            if (snd_clip(w, (unsigned)p->object, 0x60000, 0x7FFF, 0, &pan, &vol)) {
                if (p->unk_0050 == -1) p->unk_0050 = snd_loop(20, vol, pan);
                else snd_set_panvol(p->unk_0050, pan, vol, 0);
            }
        }
        if (ai_launch_missile(a, s, w, (unsigned)p->object, target, (unsigned)a->fire_missile[m->fire])) return -1;
    }
    if (!p->unk_0018) return 0;
    if (tick && m->kind == 15 && p->unk_0050 != -1) {
        snd_abort(p->unk_0050, 20); p->unk_0050 = -1;
    }
    p->unk_0018 = 0; --p->unk_0034;
    switch (p->unk_001A) {
    case 1: {
        int hit = 1;
        sound(w, p, m->sounds[3], 0x40000);
        if (s->pc_mode != 7 && target && !distance)
            return cm_fail("UpdateObjects", id, "melee reads undefined original distance");
        if (s->pc_mode != 7 && target && *distance < m->unk_0060) {
            hit = world_rnd(&w->rng, 2);
            if (overlap(o, &w->objects[target]) && (int16_t)p->unk_003C == 16) {
                world_person *t = &w->persons[p->target];
                if ((int16_t)t->unk_003C) {
                    hit = 0; sound(w, p, m->sounds[4], 0x40000);
                    word(&p->unk_003C, 6); word(&p->unk_0044, 4);
                    if ((int16_t)t->unk_003C == 6) word(&t->unk_003C, 0);
                    else { word(&t->unk_003C, 6); word(&t->unk_0044, 1); }
                } else {
                    int32_t damage = ai_do_dice(&w->rng, m->melee_damage);
                    if (damage) {
                        if ((unsigned)p->target == w->player_person) {
                            if (ai_damage_person(a, w, w->player_person, damage, 100, 100)) return -1;
                        } else if (ai_damage_monster(a, w, (unsigned)p->target, damage, p->object)) return -1;
                        hit = 1;
                    }
                    word(&p->unk_003C, 0); word(&t->unk_003C, 6); word(&t->unk_0044, 4);
                }
            }
        }
        if (p->unk_0034) {
            int n;
            word(&p->unk_003C, 16); n = world_rnd(&w->rng, 8);
            p->unk_00BC = (uint8_t)(n < 4 ? 14 : n < 6 ? 15 : 16);
        } else if (hit && world_rnd(&w->rng, 100) < m->aggression) {
            p->unk_0016 = 1; p->state = 0;
            p->unk_001E = (int16_t)(30 + world_rnd(&w->rng, 30));
        } else {
            p->state = 1; p->unk_0016 = 1;
            p->unk_001E = (int16_t)(m->unk_0066 + world_rnd(&w->rng, 5));
        }
        break;
    }
    case 0: case 2:
        if (ai_launch_missile(a, s, w, (unsigned)p->object, target,
                              (unsigned)(p->unk_001A == 2 ? a->throw_missile[m->throw_type] : a->fire_missile[m->fire]))) return -1;
        if (p->unk_0034) break;
        p->state = 1; p->unk_0016 = 1;
        if (can_move && p->unk_0028) p->unk_001E = (int16_t)(1 + world_rnd(&w->rng, 4));
        else p->unk_001E = (int16_t)(p->unk_001A == 2 ?
            m->unk_0094 + world_rnd(&w->rng, 8) : m->unk_007C + world_rnd(&w->rng, 6));
        break;
    case 3: {
        unsigned spell = m->spells[0];
        if (m->spell_count > 1) {
            do { spell = m->spells[world_rnd(&w->rng, (int32_t)m->spell_count)]; }
            while (p->unk_005C && spell == 14);
        }
        if (spell == 14) p->unk_005C += 300;
        else {
            if (spell >= 16) return cm_fail("UpdateObjects", id, "unknown spell projectile");
            if (ai_launch_missile(a, s, w, (unsigned)p->object, target, (unsigned)a->spell_missile[spell])) return -1;
        }
        p->state = 1; p->unk_0016 = 1;
        p->unk_001E = (int16_t)(can_move && p->unk_0028 ?
            2 + world_rnd(&w->rng, 5) : 20 + world_rnd(&w->rng, 20));
        break;
    }
    default: p->state = 1; p->unk_0016 = 1; p->unk_001E = 0; p->unk_0028 = 0; break;
    }
    return 0;
}

static int actor_prepare_attack(ai_state *a, sim_state *s, world *w, unsigned id,
                                 int tick, int moving, unsigned target)
{
    world_person *p = &w->persons[id];
    const ai_monster *m = &a->monsters[p->type];
    if (airborne(a, s, w, id, target)) return -1;
    if (w->scene == 7 && a->events && a->events->text_trigger && a->events->text_message == 1) {
        p->unk_001E = 100; p->state = 0; p->unk_00BC = 5; p->unk_0028 = 0; return 0;
    }
    if (m->attack[0] && !p->unk_0014) {
        anim(p, 24);
        if (ai_move_object(a, s, w, id, tick, moving)) return -1;
        if (tick) p->unk_0014 = 1;
        return 0;
    }
    if (target) ai_track_target(w, (unsigned)p->object, target, 0);
    if (!p->unk_0034) {
        switch (p->unk_001A) {
        case 1:
            if ((int16_t)p->unk_003C != 6) {
                int n;
                p->unk_0034 = (int16_t)(1 + world_rnd(&w->rng, m->unk_006A));
                p->unk_0016 = 1; n = world_rnd(&w->rng, 8);
                p->unk_00BC = (uint8_t)(n < 4 ? 14 : n < 6 ? 15 : 16);
            }
            break;
        case 0:
            p->unk_0034 = (int16_t)(1 + world_rnd(&w->rng, m->unk_007E));
            p->unk_0016 = 1;
            if (p->unk_0028 == 4 || (m->attack[5] && world_rnd(&w->rng, 2)))
                p->unk_00BC = 19;
            else p->unk_00BC = 18;
            if (!m->attack[2] && m->kind != 6 && m->kind != 14)
                p->unk_0028 = (int16_t)(p->unk_00BC == 19 ? 4 : 3);
            break;
        case 2:
            p->unk_0034 = (int16_t)(1 + world_rnd(&w->rng, m->unk_0096));
            p->unk_0016 = 1; p->unk_00BC = 17; break;
        case 3: {
            int n = world_rnd(&w->rng, 8);
            p->unk_0034 = 1; p->unk_0016 = 1;
            p->unk_00BC = (uint8_t)(n < 4 ? 21 : n < 6 ? 22 : 23); break;
        }
        default: break;
        }
    }
    if (ai_move_object(a, s, w, id, tick, p->unk_001A == 1 ? 0 : moving)) return -1;
    if (p->target >= 0 && p->unk_0034) {
        p->unk_0018 = 0; p->state = 4; p->unk_002C = p->target;
        if (p->unk_001A == 1) { word(&p->unk_003C, 16); sound(w, p, m->sounds[0], 0x40000); }
        else if (p->unk_001A == 3 && m->sounds[0] &&
                 !(a->events && a->events->text_trigger && p->object == a->events->text_object)) {
            unsigned index = m->sounds[3] && world_rnd(&w->rng, 2) ? 3u : 0u;
            sound(w, p, m->sounds[index], 0x60000);
        }
    } else { p->state = 1; p->unk_001E = 0; p->unk_0028 = 0; }
    return 0;
}

static int actor_state(ai_state *a, sim_state *s, world *w, unsigned id,
                        int tick, int moving, int fire_moving, int can_move,
                        int melee, int spells, unsigned target, const int32_t *target_distance)
{
    world_person *p = &w->persons[id];
    const ai_monster *m = &a->monsters[p->type];
    /* Other distance reads are guarded by an admitted target. Only state4
     * restores a lost target, and it checks the call-local value separately. */
    int32_t distance = target_distance ? *target_distance : p->distance;
    world_object *o = &w->objects[p->object];
    int special = w->scene == 3 && id == 82;
    int bp = s->bp_limit_flag != 0;
    int text = a->events && a->events->text_trigger;
    if (p->state != 1 && p->state != 4 && p->state != 5) p->unk_0028 = 0;
    switch (p->state) {
    case 0: case 1: {
        int initial = p->state == 0;
        if (airborne(a, s, w, id, target)) return -1;
        if (m->attack[0] && p->unk_0014) {
            anim(p, 25);
            if (ai_move_object(a, s, w, id, tick, 0)) return -1;
            if (p->unk_0018) { p->unk_0014 = 0; p->unk_0018 = 0; }
            return 0;
        }
        if (initial) {
            p->unk_00BC = 5;
            if (target && !p->unk_001E) {
                ai_track_target(w, (unsigned)p->object, target, p->unk_0020 == 0);
                if (melee && distance < m->melee_range && overlap(o, &w->objects[target]) &&
                    (!m->attack[1] || world_rnd(&w->rng, 2))) attack(p, 1);
                else if (m->attack[3] && distance < 0x24000 &&
                         (!m->attack[1] || world_rnd(&w->rng, 3) == 0)) attack(p, 2);
                else if (spells && distance < m->fire_range) attack(p, 3);
                else if (m->attack[1] && distance < m->fire_range) attack(p, 0);
                else { p->state = 1; p->unk_0028 = 0; }
            }
        } else {
            if (target) {
                if (choose_attack(a, w, p, m, o, target, melee, spells, distance) < 0) return -1;
                if (p->unk_0028 && !p->unk_001E) {
                    ai_track_target(w, (unsigned)p->object, target, 0);
                    if (p->unk_0028 == 1 || p->unk_0028 == 2) {
                        p->unk_001E = (int16_t)(p->unk_001A == 2 ?
                            m->unk_0094 + world_rnd(&w->rng, 8) : p->unk_001A == 1 ?
                            m->unk_0066 + world_rnd(&w->rng, 5) : m->unk_007C + world_rnd(&w->rng, 6));
                        p->unk_0028 = 0;
                    } else if (m->attack[3] && distance < 0x24000 &&
                               (!m->attack[1] || world_rnd(&w->rng, 3) == 0)) attack(p, 2);
                    else if (m->attack[1] && distance < m->fire_range &&
                             (world_rnd(&w->rng, 2) || !spells)) attack(p, 0);
                    else if (spells && distance < m->fire_range) attack(p, 3);
                    else { p->unk_0028 = 0; p->unk_001E = 0; }
                }
            } else {
                p->unk_0028 = 0;
                if (p->unk_0030 > 30 && !p->unk_001E) p->state = 0;
            }
            if (p->unk_0028 == 0) p->unk_00BC = 5;
            else if (p->unk_0028 == 1 || p->unk_0028 == 3) p->unk_00BC = 1;
            else if (p->unk_0028 == 2 || p->unk_0028 == 4) p->unk_00BC = 10;
        }
        if (p->unk_001E && bp) --p->unk_001E;
        return ai_move_object(a, s, w, id, tick, moving);
    }
    case 2:
        if (airborne(a, s, w, id, target)) return -1;
        p->unk_00BC = 1;
        if (ai_move_object(a, s, w, id, tick, special && p->unk_0010 > 0xE000)) return -1;
        if (p->unk_001E) { if (bp) --p->unk_001E; }
        else { p->unk_00BC = 5; p->state = 0; }
        return 0;
    case 3:
        if (airborne(a, s, w, id, target)) return -1;
        if (p->status == 2) { p->unk_001E = 0; if (!text) p->state = 8; return 0; }
        if (m->attack[0] && p->unk_0014) {
            anim(p, 25);
            if (ai_move_object(a, s, w, id, tick, 0)) return -1;
            if (p->unk_0018) { p->unk_0014 = 0; p->unk_0018 = 0; }
            return 0;
        }
        p->unk_00BC = 3; o->target_mode = 1; p->unk_006C = 0;
        o->target_angle = cm_atan64k(o->x, o->y, w->objects[w->player_object].x,
                                    w->objects[w->player_object].y);
        if (ai_move_object(a, s, w, id, tick, special && p->unk_0010 > 0xE000)) return -1;
        if (!text) {
            p->state = 2; p->unk_001E = 0; p->unk_00BC = 1;
            if (w->scene == 3 && id == 52) {
                if ((unsigned)p->target == w->player_person) p->target = -1;
                if ((o->x >> 16) == 29 && (o->y >> 16) == 1 && o->floor_z < 0x10000)
                    p->unk_001E = 50;
            }
            if (w->scene == 2 && (id == 3 || id == 68) &&
                (unsigned)p->target == w->player_person) p->target = -1;
        }
        return 0;
    case 4:
        return actor_attack(a, s, w, id, tick, moving, fire_moving, target, target_distance, can_move);
    case 5:
        return actor_prepare_attack(a, s, w, id, tick, moving, target);
    case 6:
        if (airborne(a, s, w, id, target)) return -1;
        if (p->unk_001E == 2) {
            if (tick && m->kind == 15 && p->unk_0050 != -1) {
                snd_abort(p->unk_0050, 20); p->unk_0050 = -1;
            }
            sound(w, p, m->sounds[1], 0x60000);
            if (can_move && backstep(s, w, o, 0x600)) return -1;
            if (bp) p->unk_001E = (int16_t)(p->unk_001E - world_rnd(&w->rng, 2));
        }
        if (p->unk_00BC != 26 && p->unk_00BC != 28) {
            anim(p, p->unk_00A4 ? 28u : 26u);
            if (p->unk_00A4 && (m->flags & 4)) o->rotations = -3;
        }
        if (p->unk_001E) { if (bp) --p->unk_001E; }
        else if (!p->unk_00A4) {
            int pause;
            if (p->unk_00BC == 28) {
                if (m->flags & 4) o->rotations = 8;
                p->unk_00BC = 26; p->unk_0016 = 1;
            }
            p->state = 1;
            pause = a->difficulty < 2 && can_move && m->kind != 12;
            if (++p->unk_0078 > 3) { pause = 0; p->unk_0078 = 0; }
            if (pause) {
                p->unk_0028 = 0; p->unk_001E = (int16_t)(2 + world_rnd(&w->rng, 8));
                if (!a->difficulty) p->unk_001E += 4;
            } else if (p->target >= 0) {
                if (distance < m->melee_range) { if (melee) attack(p, 1); }
                else if (m->attack[3] && distance < 0x24000 &&
                         (!m->attack[1] || world_rnd(&w->rng, 2))) attack(p, 2);
                else if (m->attack[1] && distance < m->fire_range &&
                         (world_rnd(&w->rng, 2) || !spells)) attack(p, 0);
                else if (spells && distance < m->fire_range) attack(p, 3);
            }
        }
        return ai_move_object(a, s, w, id, tick, 1);
    case 7:
        p->unk_0058 = 0;
        if (w->scene == 10 && id == 1 && w->person_count > 2 && w->persons[2].state == 9) {
            world_person *t = &w->persons[2];
            world_object *other = &w->objects[t->object];
            world_omap_delete(w, (unsigned)t->object);
            other->x = o->x; other->y = o->y; other->floor_z = o->floor_z;
            other->heading = cm_atan64k(other->x, other->y, w->objects[w->player_object].x,
                                        w->objects[w->player_object].y);
            world_omap_delete(w, (unsigned)p->object);
            o->x = o->y = 0x1F8000; word(&p->unk_0038, (int16_t)(p->unk_0038 >> 16));
            p->state = 9; world_omap_insert(w, (unsigned)p->object);
            world_omap_insert(w, (unsigned)t->object);
            t->state = 7; word(&t->unk_0038, 0); t->status = 2;
            if (m->sounds[5] && p->unk_004C != -1) {
                snd_abort(p->unk_004C, m->sounds[5]); p->unk_004C = -1;
            }
            snd_play(114, 0x7FFF, 0x8000); return 0;
        }
        if (tick && m->kind == 15 && p->unk_0050 != -1) {
            snd_abort(p->unk_0050, 20); p->unk_0050 = -1;
        }
        if (p->unk_0060 > 0) { p->unk_0060 -= 0x2000; if (p->unk_0060 < 0) p->unk_0060 = 0; }
        sound(w, p, m->sounds[2], 0x60000);
        if (can_move && backstep(s, w, o, 0x400)) return -1;
        if (p->unk_00BC != 28) {
            if (m->flags & 4) o->rotations = -3;
            anim(p, 28);
        }
        if (ai_move_object(a, s, w, id, 1, 1)) return -1;
        p->state = 8; p->unk_001E = 2; p->status = 2;
        if (m->kind == 16) return ai_launch_missile(a, s, w, (unsigned)p->object, 0, 36);
        if (m->kind == 5 || m->kind == 6 || m->kind == 17)
            return ai_launch_missile(a, s, w, (unsigned)p->object, 0, 52);
        return 0;
    case 8:
        p->unk_0058 = 0;
        if (tick && m->kind == 15 && p->unk_0050 != -1) {
            snd_abort(p->unk_0050, 20); p->unk_0050 = -1;
        }
        if (p->unk_0060 > 0) { p->unk_0060 -= 0x2000; if (p->unk_0060 < 0) p->unk_0060 = 0; }
        p->status = 2;
        if (m->kind == 16) {
            if (o->active) { o->active = 0; world_omap_delete(w, (unsigned)p->object); o->x = o->y = 0x1F8000; }
            return 0;
        }
        if (p->unk_001E) {
            if (bp) --p->unk_001E;
            if (can_move && backstep(s, w, o, 0x600)) return -1;
            return ai_move_object(a, s, w, id, tick, 0);
        }
        if (m->flags & 2) o->rotations = 1;
        p->unk_00BC = 29;
        if (ai_move_object(a, s, w, id, tick, 0)) return -1;
        if (p->unk_00A4 || p->unk_00B0 || !p->unk_0018) return 0;
        if (!p->unk_0054 && p->unk_008C > 0) {
            p->unk_0054 = 1;
            if (ai_make_soul_fire(a, w, (unsigned)p->object, p->unk_0084,
                                  p->unk_008C, p->unk_0088, p->water)) return -1;
        }
        if (p->unk_00A0 > 0) {
            int fxo = world_fxo_object(w, (unsigned)p->unk_00A0);
            if (fxo < 0) return -1;
            if (!w->objects[fxo].active) p->unk_00A0 = 0;
            else {
                world_object *drop = &w->objects[fxo];
                if (o->active) {
                    world_omap_delete(w, (unsigned)fxo); drop->x = o->x; drop->y = o->y;
                    world_omap_insert(w, (unsigned)fxo);
                }
                if (w->scene == 10 && id == 2) {
                    if (a->gem_count_down) {
                        if (add(a->gem_count_down_timer, -40) < (int32_t)a->timer_ticks && drop->floor_z < 0x2A000)
                            drop->floor_z += 0x800;
                    } else {
                        drop->floor_z = add(o->floor_z, 0x2000);
                        a->gem_count_down = 1; a->gem_count_down_timer = add((int32_t)a->timer_ticks, 120);
                    }
                } else drop->floor_z = add(o->floor_z, 0x1000);
            }
        }
        if ((p->water || m->kind == 3) && o->active) {
            o->active = 0; world_omap_delete(w, (unsigned)p->object); o->x = o->y = 0x1F8000;
        }
        return 0;
    case 9: case 10:
        p->unk_00BC = 1;
        if (ai_move_object(a, s, w, id, tick, 0)) return -1;
        if (p->state == 10 && (unsigned)p->target == w->player_person) {
            if (m->attack[3] || m->attack[1] || melee) {
                p->state = 1;
                if (ai_find_target(a, s, w, id, m->range)) return -1;
            } else { p->state = 0; p->target = -1; }
            p->unk_001E = 0;
        }
        return 0;
    case 11: case 12: return 0;
    case 13:
        if (s->pc_mode == 7) return 0;
        p->unk_0060 = 0; p->unk_00BC = 0;
        return ai_move_object(a, s, w, id, tick, 0);
    case 14:
        if (p->status == 2) p->unk_00BC = 29;
        else if (p->unk_001E) {
            if (bp) --p->unk_001E;
            if (!p->unk_001E) {
                if (target) {
                    p->unk_0034 = (int16_t)(1 + world_rnd(&w->rng, m->unk_007E));
                    p->unk_00BC = 18;
                } else p->unk_001E = 1;
            }
        } else if (target) {
            if (ai_launch_missile(a, s, w, (unsigned)p->object, target, (unsigned)a->fire_missile[m->fire])) return -1;
        } else { p->unk_00BC = 5; p->unk_0016 = 1; p->unk_001E = 1; }
        if (p->unk_0018) {
            --p->unk_0034;
            if (!p->unk_0034) {
                p->unk_001E = (int16_t)(m->unk_007C + world_rnd(&w->rng, 6));
                p->unk_00BC = 5; p->unk_0016 = 1;
            }
        }
        return ai_move_object(a, s, w, id, tick, 0);
    default: return 0;
    }
}

int ai_update_objects(ai_state *a, sim_state *s, world *w,
                       uint32_t timer_ticks, uint32_t fast_ticks)
{
    unsigned id;
    int tick = 0, fast = 0, water_damage = 0;
    int distance_known = 0;
    int32_t distance;
    if (!a->tables_loaded)
        return cm_fail("UpdateObjects", 0, "combat tables not loaded");
    a->timer_ticks = timer_ticks;
    if ((int32_t)fast_ticks > (int32_t)a->fast_animation_deadline) {
        fast = 1; a->fast_animation_deadline = fast_ticks + 3;
    }
    if ((int32_t)timer_ticks > (int32_t)a->animation_deadline) {
        tick = 1; a->animation_deadline = timer_ticks + 4;
    }
    if (a->swim_damage && (int32_t)timer_ticks > a->mon_swim_damage_time) {
        water_damage = 1;
        a->mon_swim_damage_time = (int32_t)timer_ticks + 40 + world_rnd(&w->rng, 60);
    }
    a->acquisition_phase = (a->acquisition_phase + 1) % 10;
    ai_update_pc_object(w, tick);
    for (id = 1; id < w->person_count; ++id) {
        world_person *p = &w->persons[id];
        world_object *o;
        const ai_monster *m;
        int frame_tick, can_move = 1, moving = 1, fire_moving = 0;
        int melee, spells;
        unsigned target = 0;
        int32_t min_range = 0;
        if (p->object < 0 || (unsigned)p->object >= w->object_count || ai_person_kind(a, w, id) < 0)
            return cm_fail("UpdateObjects", id, "invalid actor");
        o = &w->objects[p->object]; if (!o->active) continue;
        m = &a->monsters[p->type];
        if (m->fire >= 12 || m->throw_type >= 3)
            return cm_fail("UpdateObjects", id, "invalid monster projectile");
        frame_tick = m->kind == 17 ? 1 : m->kind == 9 ? fast : tick;
        spells = m->attack[4] != 0;
        if (a->swim_check) {
            int water = sim_coll_swim_flat(w, (unsigned)p->object, a->swim_panel, a->swim_art);
            if (water < 0) return -1;
            if (water != (p->water != 0)) snd_object(w, 4, (unsigned)p->object, 0x60000, 0x7FFF, 0);
            p->water = (int16_t)(water != 0);
            if (water) {
                o->flags |= UINT32_C(0x00001000);
                if (water_damage && a->swim_damage > 0 &&
                    m->resistance[5] < world_rnd(&w->rng, 100)) {
                    int32_t damage = a->swim_damage * (1 + world_rnd(&w->rng, 6));
                    if (ai_damage_monster(a, w, id, damage, -1)) return -1;
                }
            } else o->flags &= ~UINT32_C(0x00001000);
        }
        if (p->unk_005C) {
            o->flags |= UINT32_C(0x00020000);
            if (frame_tick && p->unk_005C > 0) {
                if (s->bp_limit_flag) --p->unk_005C;
                if (!p->unk_005C) o->flags &= ~UINT32_C(0x00020000);
                if (m->spell_count == 1 && m->spells[0] == 14) spells = 0;
            }
        }
        if (p->unk_0074) {
            if ((int32_t)timer_ticks < p->unk_0074) o->flags |= UINT32_C(0x00002000);
            else { o->flags &= ~UINT32_C(0x00002000); p->unk_0074 = 0; }
        }
        if (p->unk_0058) {
            if (s->bp_limit_flag) --p->unk_0058;
            if (sim_coll_flat(w, (unsigned)p->object, o->x, o->y)) return -1;
            if (p->unk_004C != -1) { if (m->sounds[5]) snd_abort(p->unk_004C, m->sounds[5]); p->unk_004C = -1; }
            if (p->unk_0050 != -1) { snd_abort(p->unk_0050, 20); p->unk_0050 = -1; }
            if (p->state != 6 && p->state != 7 && p->state != 8) {
                ai_track_target(w, (unsigned)p->object, w->player_object, 0);
                if (p->unk_00BC != 29) {
                    if (m->flags & 2) o->rotations = 1;
                    p->unk_00BC = 29; p->unk_0016 = 1;
                    if (ai_move_object(a, s, w, id, frame_tick, 0)) return -1;
                }
                continue;
            }
        }
        if (w->scene == 3 && id == 82 && !p->unk_0060 && o->floor_z < 0x10000) {
            o->floor_z = 0x10000; p->unk_0060 = 0x7000;
        }
        if (p->unk_00BC != 28 && p->unk_00BC != 29) o->rotations = 8;
        switch (m->kind) {
        case 3: o->rotations = 1; break;
        case 5: min_range = 0x10000; break;
        case 6: can_move = 0; break;
        case 9: fire_moving = 1; break;
        case 14: p->state = 14; min_range = 0x10000; can_move = 0; break;
        default: break;
        }
        if (w->scene == 3 && id == 82 && p->state != 6 && a->events &&
            a->events->text_trigger && p->object == a->events->text_object && a->events->text_message == 7)
            p->state = 3;
        if (p->state == 9 || p->state == 10 || p->state == 13 || p->status == 2) {
            p->target = -1;
            if (m->sounds[5] && p->unk_004C != -1) {
                snd_abort(p->unk_004C, m->sounds[5]); p->unk_004C = -1;
            }
            if (p->state == 10 && ai_sight_person(a, s, w, id, w->player_person, -1)) return -1;
        } else {
            if (frame_tick) {
                if (p->unk_0020) --p->unk_0020;
                if (p->unk_0024) --p->unk_0024;
                else {
                    if (world_rnd(&w->rng, 100) >= m->accuracy) p->unk_0020 = 16;
                    p->unk_0024 = 16;
                }
            }
            if ((int16_t)p->unk_003C == 6) {
                if ((int16_t)p->unk_0044) word(&p->unk_0044, (int16_t)(p->unk_0044 - 1));
                else word(&p->unk_003C, 0);
            }
            if (o->target_mode) {
                o->heading = sim_turn(o->heading, o->target_angle, m->turn_speed);
                if (o->heading == o->target_angle) o->target_mode = 0;
            }
            if (p->target == -1) {
                if ((int32_t)(id % 10) == a->acquisition_phase && ai_find_target(a, s, w, id, m->range)) return -1;
            } else {
                if (p->target < 0 || (unsigned)p->target >= w->person_count)
                    return cm_fail("UpdateObjects", id, "invalid target person");
                if (ai_sight_person(a, s, w, id, (unsigned)p->target, m->range)) return -1;
            }
            p->unk_0010 = cm_distance(o->x, o->y, w->objects[w->player_object].x,
                                      w->objects[w->player_object].y);
            if (m->sounds[5]) {
                if (p->unk_0010 < m->sound_range && ((m->flags & 1) || p->state == 0 || p->state == 1)) {
                    unsigned pan;
                    int volume;
                    if (snd_clip(w, (unsigned)p->object, m->sound_range, 0x4000, 0, &pan, &volume)) {
                        if (p->unk_004C == -1) p->unk_004C = snd_loop(m->sounds[5], volume, pan);
                        else snd_set_panvol(p->unk_004C, pan, volume, 0);
                    }
                } else if (p->unk_004C != -1) {
                    snd_abort(p->unk_004C, m->sounds[5]); p->unk_004C = -1;
                }
            }
            if (frame_tick && m->sounds[6] && p->unk_0010 < m->sound_range &&
                ((m->flags & 1) || p->state == 0 || p->state == 1) && world_rnd(&w->rng, 30) == 15) {
                unsigned index = m->sounds[7] ? (unsigned)world_rnd(&w->rng, 2) : 0;
                sound(w, p, m->sounds[6 + index], m->sound_range);
            }
            if (p->unk_0090 > 0 && a->events && !a->events->text_trigger &&
                (p->state == 0 || p->state == 1) && p->unk_0010 < 0x28000 &&
                (int32_t)timer_ticks > p->unk_009C) {
                int temporary = 0;
                if (m->rank[0] == 0 && p->unk_0010 < 0x18000) {
                    if (ai_sight_person(a, s, w, id, w->player_person, m->range)) return -1;
                    temporary = 1;
                }
                if ((unsigned)p->target == w->player_person || (w->scene == 2 && id == 57)) {
                    if (temporary) p->state = 3;
                    a->events->text_message = add(p->unk_0090, world_rnd(&w->rng, p->unk_0094));
                    a->events->text_object = p->object; a->events->text_trigger = 2;
                    snd_message(w, w->scene, (unsigned)a->events->text_message, p->object);
                    if (p->unk_0098) p->unk_009C = (int32_t)(timer_ticks + (uint32_t)p->unk_0098 * 40u);
                    else p->unk_0090 = 0;
                }
                if (temporary) p->target = -1;
            }
        }
        melee = m->attack[2];
        if (p->target >= 0) {
            const world_person *t = &w->persons[p->target];
            int kind = ai_person_kind(a, w, (unsigned)p->target);
            target = (unsigned)t->object; distance = p->distance; p->unk_0030 = 0;
            /* Original035240 writes one call-local stack slot. Lost-target
             * state4 restores its target but retains the preceding actor's
             * distance, not this actor's stale PERSON+0C value. */
            distance_known = 1;
            if (kind == 5 || kind == 17 || kind == 6) melee = 0;
            if (target == w->player_object && distance < min_range) moving = 0;
        } else { ++p->unk_0030; target = 0; }
        if (actor_state(a, s, w, id, frame_tick, moving, fire_moving, can_move,
                         melee, spells, target, distance_known ? &distance : NULL)) return -1;
    }
    return 0;
}
