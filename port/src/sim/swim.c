#include "sim_internal.h"
#include "ai.h"
#include "audio.h"

/* HandleNextFrame 01CAC0..01CD80; shared deadline also gates floor fire. */
int sim_swim_step(sim_state *s, world *w)
{
    world_object *o;
    int touching = 0, moving = 0;
    if (!s->swim_check) return 0;
    if (w->player_object >= w->object_count || w->player_person >= w->person_count)
        return cm_fail("Swim", w->player_object, "invalid player object/person");
    o = &w->objects[w->player_object];
    if (!s->unk_144b1c && !s->flight_mode && s->pc_mode != 7) {
        touching = sim_coll_swim_flat(w, w->player_object, s->swim_panel, s->swim_art);
        if (touching < 0) return -1;
    }
    if (touching) {
        int32_t speed = s->unk_144aec;
        if (!s->unk_144b50) {
            (void)snd_play(4, 32767, 32768);
            s->swim_damage_time = s->timer_ticks + 20u + (uint32_t)world_rnd(&w->rng, 40);
        }
        s->unk_144b50 = 1;
        if (!s->unk_144afc) s->unk_144afc = 1;
        if ((speed > 4 || speed < -4) &&
            (s->pc_mode == 1 || s->pc_mode == 5 ||
             (s->pc_mode == 2 && (!s->ai || s->ai->pc.auto_s_flag || !s->ai->pc.fire))))
            moving = 1;
        s->unk_144b3c = 0;
        s->unk_144b34 = w->unk_5E7A8 ? -0x3000 : -0x4800;
        s->body_height = sim_add32(sim_add32(sim_add32(o->z, o->z / 2), 0x1000), s->unk_144b34);
        if (s->swim_damage && sim_wrap32(s->timer_ticks) > sim_wrap32(s->swim_damage_time)) {
            if (s->swim_damage > 0) {
                int32_t damage;
                if (!s->ai) return cm_fail("SwimDamage", 0, "missing actor state");
                if (s->ai->pc_flash < 2) s->ai->pc_flash = 2;
                damage = sim_wrap32((uint32_t)s->swim_damage * (uint32_t)(1 + world_rnd(&w->rng, 6)));
                if (ai_damage_person(s->ai, w, w->player_person, damage, 100, 125)) return -1;
            } else {
                world_person *p = &w->persons[0];
                int16_t hp = (int16_t)(uint16_t)p->unk_0038;
                int16_t maximum = (int16_t)((uint32_t)p->unk_0038 >> 16);
                if (hp < maximum) {
                    (void)snd_play(27, 32767, 32768);
                    hp = (int16_t)(uint16_t)((uint32_t)(uint16_t)hp + (uint32_t)world_rnd(&w->rng, 10));
                    if (hp > maximum) hp = maximum;
                    p->unk_0038 = sim_wrap32(((uint32_t)(uint16_t)maximum << 16) | (uint16_t)hp);
                }
            }
            s->swim_damage_time = s->timer_ticks + 40u + (uint32_t)world_rnd(&w->rng, 60);
        }
    } else {
        if (s->unk_144b50) s->unk_144b3c = 1;
        s->unk_144b50 = 0;
    }
    if (moving) {
        if (s->swim_loop == -1) s->swim_loop = snd_loop(4, 16384, 32768);
    } else if (s->swim_loop != -1) {
        if (snd_done(s->swim_exit_sound, 3))
            s->swim_exit_sound = snd_play(3, 16384 - world_rnd(&w->rng, 8192), 32768);
        snd_abort(s->swim_loop, 4);
        s->swim_loop = -1;
    }
    return 0;
}
