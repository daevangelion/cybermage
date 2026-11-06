#include "combat_internal.h"
#include "audio.h"
#include "events.h"
#include <limits.h>

/* CM_HI.EXE UpdateMissile030B38..033ECE. The switch at030A68 has52
 * original handlers; helpers below share the repeated movement/animation
 * rules, not a translated register machine. */
static void die(ai_missile *m, world_object *o) { m->active = 0; o->active = 0; }
static int inside(const world_object *o)
{
    return o->x >= 0x10000 && o->x <= 0x1f0000 && o->y >= 0x10000 && o->y <= 0x1f0000;
}
static void animate(ai_missile *m, world_object *o, int frames, int stride)
{
    o->texture = (uint16_t)(o->texture + stride);
    m->frame = (m->frame + 1) % frames;
    if (!m->frame) o->texture = (uint16_t)(o->texture - frames * stride);
}
static int learn(ai_state *a, unsigned spell)
{
    if (!a->events || spell >= 64) return cm_fail("LearnSpell", spell, "invalid spell inventory");
    a->events->spells[spell / 8] |= (uint8_t)(1u << (spell & 7)); return 0;
}
static int person_of(const world *w, int object)
{
    if (object <= 0 || (unsigned)object >= w->object_count || w->objects[object].targetable != 1) return -1;
    return w->objects[object].person;
}
static int hit_move(ai_state *a, sim_state *s, world *w, unsigned slot, int face, int *hit)
{
    ai_missile *m = &a->missiles[slot];
    world_object *o = &w->objects[m->object];
    *hit = sim_coll_fire(s, w, slot);
    if (*hit == INT_MIN) return -1;
    if (*hit) {
        if (face && ai_face_shooter(a, w, (unsigned)m->source, *hit)) return -1;
        return 2;
    }
    if (!inside(o)) { die(m, o); return 0; }
    if (!m->ticks) return 2;
    if (s->bp_limit_flag) m->ticks = ai_sub32(m->ticks,1);
    return 1;
}
static int pc_blast(ai_state *a, sim_state *s, world *w, ai_missile *m,
                     unsigned zone, unsigned dice, int twice, int armor, int erosion, int flash)
{
    int damage = ai_pc_radial_damage(a, s, w, (unsigned)m->object, zone, dice, twice, armor, erosion);
    if (damage < 0) return -1;
    if (damage) a->pc_flash = flash;
    return 0;
}
static int blast(ai_state *a, sim_state *s, world *w, ai_missile *m,
                  unsigned zone, unsigned dice, unsigned resistance, int force)
{
    return ai_radial_damage(a, s, w, (unsigned)m->object, zone, m->source, dice, resistance, force);
}
static void effect(ai_missile *m, int type, int dice, int zone, int force, int life)
{
    m->type = type; m->variant = dice; m->ticks = zone; m->radius = force; m->vertical = life;
}
static void boom_sound(const world *w, const ai_missile *m, unsigned sample, int volume)
{
    snd_object(w, sample, (unsigned)m->object, 0x60000, volume, 0);
}
static int loop_sound(world *w, ai_missile *m, unsigned sample)
{
    const world_object *pc = &w->objects[w->player_object], *o = &w->objects[m->object];
    int distance = cm_distance(pc->x, pc->y, o->x, o->y);
    if (distance < 0x40000) {
        int volume = 32767 - distance / 8;
        if (m->sound == -1) m->sound = snd_loop(sample, volume, 32768);
        else if (snd_set_volume(m->sound, volume)) return -1;
    } else if (m->sound != -1) { snd_abort(m->sound, sample); m->sound = -1; }
    return 0;
}
static void stop_loop(ai_missile *m, unsigned sample)
{
    if (m->sound != -1) snd_abort(m->sound, sample);
    m->sound = -1;
}
static int target_damage(ai_state *a, world *w, ai_missile *m,
                          unsigned row, unsigned resistance, int armor, int erosion, int spell)
{
    int person = person_of(w, m->target), damage;
    if (person < 0) return 0;
    if ((unsigned)person >= w->person_count) return cm_fail("UpdateMissile", (uint32_t)m->target, "invalid attached PERSON");
    damage = ai_do_dice(&w->rng, a->zone_dice[row][2]);
    if (damage && (unsigned)person != w->player_person) {
        int type = w->persons[person].type;
        if (type < 0 || (unsigned)type >= a->monster_count) return cm_fail("UpdateMissile", (uint32_t)person, "invalid monster type");
        damage = ai_scale32(damage, 100 - a->monsters[type].resistance[resistance], 100);
    }
    if (!damage) return 0;
    if ((unsigned)person == w->player_person) {
        if (spell >= 0 && learn(a, (unsigned)spell)) return -1;
        return ai_damage_person(a, w, (unsigned)person, damage, armor, erosion);
    }
    if (ai_damage_monster(a, w, (unsigned)person, damage, m->source)) return -1;
    if (w->persons[person].status == 2) m->vertical = 0;
    return 0;
}
static void follow(world *w, ai_missile *m, int face_player, int set_z)
{
    world_object *o = &w->objects[m->object];
    const world_object *target, *pc;
    int32_t radius;
    if (person_of(w, m->target) < 0) return;
    target = &w->objects[m->target]; pc = &w->objects[w->player_object];
    if (face_player) o->heading = (unsigned)m->target == w->player_object ? target->heading :
                                 cm_atan64k(target->x, target->y, pc->x, pc->y);
    radius = face_player ? (m->ticks < 0x1000 ? m->ticks = 0x1000 : m->ticks) : 0x2800;
    o->x = ai_add32(target->x, ai_fixed32(w->trig.cos[o->heading >> 5], radius));
    o->y = ai_add32(target->y, ai_fixed32(w->trig.sin[o->heading >> 5], radius));
    if (set_z) o->floor_z = ai_add32(ai_add32(target->floor_z, target->z), 0x1000);
    else if (face_player) {
        int32_t low = ai_add32(target->floor_z, 0x1000);
        int32_t high = ai_add32(ai_add32(target->floor_z, target->z), target->height / 2);
        if (o->floor_z < low) o->floor_z = low;
        else if (o->floor_z > high) o->floor_z = high;
    }
}

int ai_update_missile(ai_state *a, sim_state *s, world *w)
{
    unsigned slot;
    int fade;
    if (!a->tables_loaded || w->player_object >= w->object_count)
        return cm_fail("UpdateMissile", 0, "missile tables/player not initialized");
    fade = (int32_t)a->timer_ticks > (int32_t)a->missile_fade_time;
    if (fade) a->missile_fade_time = a->timer_ticks + 10;
    ai_make_missile_list(a, w);
    for (slot = 0; slot < AI_MISSILE_MAX; ++slot) {
        ai_missile *m = &a->missiles[slot];
        world_object *o;
        int state, hit = 0, type, keep = 1;
        if (!m->active) continue;
        if (m->object < 0 || (unsigned)m->object >= w->object_count)
            return cm_fail("UpdateMissile", slot, "invalid missile object");
        o = &w->objects[m->object];
        if (o->active) world_omap_delete(w, (unsigned)m->object); else o->active = 1;
        a->pc_learn_spell = -1; type = m->type;
        switch (type) {
        case 1:
            hit = sim_coll_fire(s, w, slot);
            if (hit == INT_MIN) return -1;
            if (hit) {
                if (ai_face_shooter(a, w, (unsigned)m->source, hit) || pc_blast(a,s,w,m,2,1,1,150,100,4)) return -1;
                boom_sound(w,m,13,0x9fff); effect(m,8,1,2,0x8000,8);
                if (ai_set_missile(w,(unsigned)m->object,9)) return -1;
                o->z = 0x2800;
            } else if (!inside(o)) die(m,o);
            else if (m->ticks) {
                if (s->bp_limit_flag) {
                    m->ticks = ai_sub32(m->ticks,1);
                    if (!m->ticks) m->vertical = 0x1000;
                }
            } else m->vertical = ai_add32(m->vertical,m->vertical);
            break;
        case 38:
            animate(m,o,8,1); state = 0;
            if (m->ticks > 0) {
                hit = sim_coll_fire(s,w,slot); if (hit == INT_MIN) return -1;
                if (hit) { if (ai_face_shooter(a,w,(unsigned)m->source,hit)) return -1; state = 1; }
                else if (!inside(o)) die(m,o);
                else if (s->bp_limit_flag && !--m->ticks) m->ticks = -80;
            } else if (m->ticks < 0) {
                unsigned i;
                ++m->ticks;
                for (i = 0; i < w->person_count; ++i) if (w->persons[i].status != 2) {
                    int id = w->persons[i].object;
                    int64_t dz;
                    if (id < 0 || (unsigned)id >= w->object_count) return cm_fail("UpdateMissile",i,"invalid mine target");
                    dz = (int64_t)o->floor_z - w->objects[id].floor_z - w->objects[id].z;
                    if (dz < 0) dz = -dz;
                    if (cm_distance(o->x,o->y,w->objects[id].x,w->objects[id].y) < 0x7000 && dz < 0x7000) {
                        if (m->ticks < -2) m->ticks = -2; else state = 1;
                    }
                }
            } else state = 1;
            if (state) {
                if (pc_blast(a,s,w,m,4,2,0,125,100,4)) return -1;
                boom_sound(w,m,14,0x9fff); effect(m,5,2,4,0xa000,6);
                if (ai_set_missile(w,(unsigned)m->object,7)) return -1;
                o->flags |= 0x8000;
            }
            break;
        case 2: case 17: case 19: case 49:
            state = hit_move(a,s,w,slot,0,&hit); if (state < 0) return -1;
            if (state == 2) {
                if (hit == (int)w->player_object) {
                    int person = m->source >= 0 && (unsigned)m->source < w->object_count ? w->objects[m->source].person : -1;
                    int damage;
                    if (a->pc_flash <= 2) a->pc_flash = world_rnd(&w->rng,3) + 2;
                    if (person > 0) {
                        int definition;
                        ai_dice d;
                        if ((unsigned)person >= w->person_count)
                            return cm_fail("UpdateMissile",slot,"invalid shooter PERSON");
                        definition = w->persons[person].type;
                        if (definition < 0 || (unsigned)definition >= a->monster_count)
                            return cm_fail("UpdateMissile",slot,"invalid shooter definition");
                        d.count = a->monsters[definition].unk_0082; d.sides = a->monsters[definition].unk_0083; d.add = a->monsters[definition].unk_0084;
                        damage = ai_do_dice(&w->rng,d);
                    } else damage = world_rnd(&w->rng,5) + 2;
                    if (ai_damage_person(a,w,w->player_person,damage,100,50)) return -1;
                }
                m->type = hit ? 3 : 4; m->vertical = 4;
                if (ai_set_missile(w,(unsigned)m->object,6)) return -1;
            }
            break;
        case 3:
            if (person_of(w,m->target) >= 0 && (unsigned)m->target != w->player_object) {
                if (ai_damage_monster(a,w,(unsigned)w->objects[m->target].person,world_rnd(&w->rng,6)+5,m->source) ||
                    ai_face_shooter(a,w,(unsigned)m->source,m->target)) return -1;
            }
            m->type = 4;
            /* Original033C96 falls into the same spark follow state. */
            /* fall through */
        case 4:
            if (!m->vertical) die(m,o);
            else {
                if (m->target > 0 && (unsigned)m->target < w->object_count) {
                    const world_object *t = &w->objects[m->target];
                    if (m->ticks < 0x1000) m->ticks = 0x1000;
                    o->x = ai_add32(t->x,ai_fixed32(w->trig.cos[o->heading>>5],m->ticks));
                    o->y = ai_add32(t->y,ai_fixed32(w->trig.sin[o->heading>>5],m->ticks));
                }
                ++o->texture; m->vertical = ai_sub32(m->vertical,1);
            }
            break;
        case 5: case 8: case 10: case 13: case 16: case 28: case 30: case 41:
            if (!m->vertical) die(m,o);
            else {
                if (m->active == 1) {
                    if (type != 13 && type != 30 && blast(a,s,w,m,(unsigned)m->ticks,(unsigned)m->variant,type==16?1u:3u,m->radius)) return -1;
                    m->active = 2;
                }
                ++o->texture; m->vertical = ai_sub32(m->vertical,1);
            }
            break;
        case 34: case 52:
            if (type == 34) a->pc_learn_spell = 8;
            if (type == 52) {
                const world_object *from;
                if (m->source < 0 || (unsigned)m->source >= w->object_count) return cm_fail("UpdateMissile",slot,"invalid firestorm source");
                from = &w->objects[m->source]; o->floor_z = ai_add32(ai_add32(from->floor_z,from->z),from->height/4);
            }
            if (m->vertical) {
                if (s->bp_limit_flag && ai_launch_missile(a,s,w,(unsigned)m->object,(unsigned)world_rnd(&w->rng,8),42)) return -1;
                if (fade && (pc_blast(a,s,w,m,3,7,0,100,100,4) || blast(a,s,w,m,(unsigned)m->ticks,7,3,0xa000))) return -1;
                animate(m,o,3,1); if (s->bp_limit_flag) m->vertical = ai_sub32(m->vertical,1);
            } else {
                if (pc_blast(a,s,w,m,3,7,0,100,100,4)) return -1;
                boom_sound(w,m,14,0x9fff); effect(m,5,7,3,0xa000,6);
                if (ai_set_missile(w,(unsigned)m->object,7)) return -1;
                o->flags |= 0x8000;
            }
            break;
        case 42:
            ++o->texture; hit = sim_coll_fire(s,w,slot); if (hit == INT_MIN) return -1;
            if (!inside(o)) die(m,o);
            else {
                if (pc_blast(a,s,w,m,3,7,0,100,100,4)) return -1;
                boom_sound(w,m,14,0x9fff); effect(m,5,7,3,0xa000,5); o->flags |= 0x8000;
            }
            break;
        case 29: case 31:
            if (!m->vertical) die(m,o);
            else {
                if (fade && (type == 29 || (person_of(w,m->target) >= 0 && world_rnd(&w->rng,2))))
                    if (target_damage(a,w,m,type==29?9u:10u,type==29?2u:5u,type==29?25:100,type==29?50:150,type==29?2:-1)) return -1;
                follow(w,m,1,0); animate(m,o,type==29?5:4,1);
                if (m->vertical && s->bp_limit_flag) m->vertical = ai_sub32(m->vertical,1);
            }
            break;
        case 12: case 32: case 33: case 35: case 47:
            a->pc_learn_spell = type==12 ? 1 : type==32 ? 7 : type==35 ? 12 : -1;
            if (!m->vertical) {
                if (type==32 || type==35) stop_loop(m,type==32?26u:28u);
                if (type==12) { effect(m,13,m->variant,m->ticks,0,6); if (ai_set_missile(w,(unsigned)m->object,12)) return -1; }
                else die(m,o);
            } else {
                unsigned zone = type==12?2u:type==33?1u:type==47?2u:3u;
                unsigned dice = type==12?4u:type==32?14u:type==33?11u:type==35?16u:21u;
                unsigned resistance = type==12?0u:type==32?6u:2u;
                int armor = type==12||type==32||type==35?0:type==47?50:100;
                int erosion = armor;
                if ((type==32||type==35) && loop_sound(w,m,type==32?26u:28u)) return -1;
                if (fade && (pc_blast(a,s,w,m,zone,dice,0,armor,erosion,type==35?3:2) ||
                    blast(a,s,w,m,type==35||type==32?3u:(unsigned)m->ticks,dice,resistance,type==35?0x6000:0))) return -1;
                if (type==33 || type==47) follow(w,m,0,type==47);
                animate(m,o,type==12?6:4,1); if (s->bp_limit_flag) m->vertical = ai_sub32(m->vertical,1);
            }
            break;
        case 36: case 37: case 48:
            /* Original jump-table entries go straight to033E57: no reinsertion. */
            keep = 0;
            break;
        default:
            {
                unsigned frames = type==14?3u:type==15||type==20||type==24||type==25||type==26||type==39?4u:
                                  type==11||type==46?6u:type==21?5u:type==23?2u:type==27?3u:type==50?8u:0u;
                unsigned spell = type==11?1u:type==21?2u:type==22?3u:type==23?4u:type==26?7u:type==27?8u:type==39?12u:64u;
                int periodic = type==11||type==26||type==27||type==39;
                if (type < 1 || type > 52) { keep = 0; break; }
                if (spell < 64) a->pc_learn_spell = (int32_t)spell;
                if ((type==26||type==39) && loop_sound(w,m,type==26?26u:28u)) return -1;
                if (frames) animate(m,o,(int)frames,type==14||type==23?16:1);
                state = hit_move(a,s,w,slot,type!=23&&type!=39,&hit); if (state<0) return -1;
                if (hit == (int)w->player_object && spell < 64 && learn(a,spell)) return -1;
                if (!state && (type==26||type==39)) stop_loop(m,type==26?26u:28u);
                if (state==1 && periodic && fade) {
                    unsigned dice=type==11?4u:type==26?14u:type==27?7u:16u;
                    unsigned zone=type==11?2u:3u, res=type==11?0u:type==26?6u:type==27?3u:2u;
                    int force=type==27?0xa000:type==39?0x6000:0;
                    if ((unsigned)m->source!=w->player_object && pc_blast(a,s,w,m,zone,dice,0,type==27?100:0,type==27?100:0,type==27?4:type==39?3:2)) return -1;
                    if (blast(a,s,w,m,type==11?(unsigned)(m->ticks + (s->bp_limit_flag ? 1 : 0)):3u,dice,res,force)) return -1;
                }
                if (state==1 && type==46 && s->bp_limit_flag && !world_rnd(&w->rng,2))
                    if (ai_launch_missile(a,s,w,(unsigned)m->object,(unsigned)m->target,48)) return -1;
                if (state!=2) break;
                switch(type) {
                case 11: effect(m,12,4,2,m->radius,80); break;
                case 21: case 25:
                    if (hit<=0 || person_of(w,hit)<0) die(m,o);
                    else effect(m,type==21?29:31,type==21?9:10,0,m->radius,type==21?50:30);
                    break;
                case 39: case 26:
                    {
                        unsigned dice=type==39?16u:14u;
                        if (pc_blast(a,s,w,m,3,dice,0,0,0,type==39?3:2) || blast(a,s,w,m,3,dice,type==39?2u:6u,type==39?0x6000:0)) return -1;
                        effect(m,type==39?35:32,(int)dice,3,type==39?0x6000:0,type==39?80:100);
                    }
                    break;
                case 27:
                    if (pc_blast(a,s,w,m,3,7,0,100,100,4) || ai_launch_missile(a,s,w,(unsigned)m->object,(unsigned)world_rnd(&w->rng,8),42)) return -1;
                    effect(m,34,7,3,0xa000,4); break;
                case 23:
                    if (hit>0 && person_of(w,hit)>=0) {
                        unsigned person=(unsigned)w->objects[hit].person;
                        int damage=world_rnd(&w->rng,10); damage+=world_rnd(&w->rng,10)+5;
                        if (person==w->player_person) { if (learn(a,4)||ai_damage_person(a,w,person,damage,100,100)) return -1; }
                        else {
                            int kind=ai_person_kind(a,w,person);
                            if (kind==16||kind==6||kind==5||kind==14||kind==13||kind==17) damage*=3;
                            if (ai_damage_monster(a,w,person,damage,m->source)||ai_blast_physics(a,w,(unsigned)hit,(unsigned)m->object,0x5000)||ai_freeze_monster(a,s,w,(unsigned)hit,2,12)) return -1;
                        }
                    }
                    boom_sound(w,m,14,0x9fff); effect(m,30,12,0,0,7);
                    if (ai_set_missile(w,(unsigned)m->object,24)) return -1;
                    break;
                case 24: effect(m,33,11,1,m->radius,10); break;
                case 46:
                    if (!hit) { die(m,o); break; }
                    if (ai_set_missile(w,(unsigned)m->object,32)) return -1;
                    boom_sound(w,m,28,0x7fff); effect(m,47,m->variant,2,m->radius,10); m->frame=0; break;
                case 9: case 40: case 51:
                    boom_sound(w,m,14,0x9fff);
                    if (pc_blast(a,s,w,m,type==40?1u:0u,type==9?3u:type==40?15u:22u,0,100,type==40?75:100,4)) return -1;
                    effect(m,type==40?41:10,type==9?3:type==40?15:22,type==40?1:0,type==40?0x4000:0x2000,6);
                    if (ai_set_missile(w,(unsigned)m->object,1)) return -1;
                    break;
                case 18: case 43: case 44: case 45:
                    {
                        int dice=type==18?8:type==43?18:type==44?19:20;
                        int zone=type==43||type==44?2:1;
                        if (pc_blast(a,s,w,m,(unsigned)zone,(unsigned)dice,0,100,75,3)) return -1;
                        effect(m,28,dice,zone,zone==2?0x7000:0x5000,6); boom_sound(w,m,14,0x9fff);
                        if (ai_set_missile(w,(unsigned)m->object,18)) return -1;
                    }
                    break;
                case 15: case 20:
                    if (pc_blast(a,s,w,m,1,type==15?5u:6u,0,80,80,3)) return -1;
                    if(type==20) boom_sound(w,m,14,0x9fff);
                    effect(m,type==15?16:28,type==15?5:6,1,type==15?0:0x4000,6);
                    if (ai_set_missile(w,(unsigned)m->object,type==15?12u:18u)) return -1;
                    break;
                case 22: case 50:
                    if (pc_blast(a,s,w,m,1,13,0,100,50,4)) return -1;
                    boom_sound(w,m,14,0x9fff); effect(m,5,13,1,0x8000,6);
                    if (ai_set_missile(w,(unsigned)m->object,7)) return -1;
                    o->flags|=0x8000;
                    break;
                case 6: case 7: case 14:
                    if (pc_blast(a,s,w,m,3,0,0,100,100,4)) return -1;
                    boom_sound(w,m,14,0x9fff); effect(m,5,type==6?17:type==14?23:0,type==14?2:3,type==6?0xa000:type==14?0x8000:0x6000,6);
                    if (ai_set_missile(w,(unsigned)m->object,7)) return -1;
                    o->flags|=0x8000;
                    break;
                default: return cm_fail("UpdateMissile",(uint32_t)type,"unmapped original projectile handler");
                }
            }
            break;
        }
        if (keep && m->active) world_omap_insert(w,(unsigned)m->object);
    }
    return 0;
}
