#include "ai.h"
#include "audio.h"
#include "events.h"

/* Word writes preserve the adjacent original PERSON short. */
static int32_t low_word(int32_t value, int16_t word)
{
    return (int32_t)(((uint32_t)value & UINT32_C(0xFFFF0000)) | (uint16_t)word);
}

int ai_blast_physics(const ai_state *a, world *w, unsigned target,
                     unsigned source, int32_t force)
{
    world_object *o;
    const world_object *s;
    world_person *p;
    int kind;
    int32_t distance, height;
    if (target >= w->object_count || source >= w->object_count)
        return cm_fail("BlastPhysics", target, "invalid object");
    o = &w->objects[target]; s = &w->objects[source];
    if (o->person < 0 || (unsigned)o->person >= w->person_count)
        return cm_fail("BlastPhysics", target, "object has no person");
    p = &w->persons[o->person];
    kind = ai_person_kind(a, w, (unsigned)o->person);
    if (kind < 0) return cm_fail("BlastPhysics", target, "invalid monster");
    if ((p->unk_00B8 & 2) || kind == 14 || kind == 17 || kind == 5 || kind == 6)
        return 0;
    if (p->status != 2) {
        if (p->unk_00B8 & 8) return 0;
        force /= 2;
    }
    p->unk_00B4 = (int16_t)cm_atan64k(s->x, s->y, o->x, o->y);
    distance = cm_distance(s->x, s->y, o->x, o->y);
    if (distance < 0x1000) distance = 0x1000;
    p->unk_00A8 = (int32_t)((int64_t)force * 0x1000 / distance);
    height = (int32_t)((uint32_t)o->height * 2u + (uint32_t)o->floor_z -
                       (uint32_t)s->floor_z);
    p->unk_00AC = (int32_t)((int64_t)(height >> 3) * force / distance);
    if (w->unk_5E7A8) { p->unk_00A8 /= 2; p->unk_00AC /= 2; }
    if (p->unk_00AC < 0 || p->water) p->unk_00AC = 0;
    p->unk_00B0 = p->unk_00AC;
    p->unk_00A4 = force / 0x1000 + 2;
    return 0;
}

/* Xox::DamageMonster 0x2AFCC; no player difficulty reduction here. */
int ai_damage_monster(ai_state *a, world *w, unsigned person,
                      int32_t damage, int32_t shooter)
{
    world_person *p;
    int16_t health;
    if (person >= w->person_count)
        return cm_fail("DamageMonster", person, "invalid person");
    p = &w->persons[person];
    if (p->status == 2 || person == w->player_person) return 0;
    if (p->unk_00B8 & 2) {
        if (shooter == (int32_t)w->player_object)
            p->unk_0074 = (int32_t)(a->timer_ticks + 80u);
        return 0;
    }
    p->unk_0058 = 0;
    if (a->events && (p->unk_00B8 & 4) && p->object == a->events->text_object &&
        shooter == (int32_t)w->player_object && a->events->text_trigger) {
        a->events->text_trigger = 0;
        snd_message_stop();
    }
    health = (int16_t)p->unk_0038;
    if (p->state == 13) {
        snd_object(w, 31, (unsigned)p->object, 0x60000, 0x7FFF, 0);
        if (health > 0) {
            health = (int16_t)((uint16_t)health - (uint32_t)damage);
            if (health < 6) health = (int16_t)(4 + world_rnd(&w->rng, 3));
        } else { p->state = 8; p->status = 2; p->unk_001E = 0; health = 0; }
    } else {
        health = (int16_t)((uint16_t)health - (uint32_t)damage);
        if (health <= 0) { p->state = 7; p->status = 2; health = 0; }
        else if (!(w->scene == 10 && person == 1 && world_rnd(&w->rng, 3) == 0)) {
            p->state = 6;
            p->unk_001E = 2;
        }
    }
    p->unk_0038 = low_word(p->unk_0038, health);
    return 0;
}

/* DamagePerson 0x2B11C. Vehicle damage can spill into the foot PERSON. */
int ai_damage_person(ai_state *a, world *w, unsigned person, int32_t damage,
                     int32_t armor_percent, int32_t erosion_percent)
{
    world_person *p;
    int16_t hp;
    unsigned body;
    int32_t armor, absorbed = 0, erosion = 0;
    if (person >= w->person_count || !a->events)
        return cm_fail("DamagePerson", person, "invalid person or missing inventory");
    if (person) {
        damage -= world_rnd(&w->rng, 4) + 4;
        if (a->difficulty == 0) damage -= world_rnd(&w->rng, 4);
        if (damage <= 0) return 0;
        p = &w->persons[person];
        hp = (int16_t)((uint16_t)p->unk_0038 - (uint32_t)damage);
        if (hp > 0) { p->unk_0038 = low_word(p->unk_0038, hp); return 0; }
        p->status = 2; p->state = 8; p->unk_001E = 0;
        p->unk_0038 = low_word(p->unk_0038, 0);
        if (snd_done(a->pc_sound, 0)) a->pc_sound = snd_play(0, 0x7FFF, 0x8000);
    }
    p = &w->persons[0];
    if (p->unk_0074) return 0;
    /* Easy halves then doubles with truncation; this is not a no-op for odd damage. */
    if (a->difficulty == 0) damage = (damage / 2) * 2;
    else if (a->difficulty == 2) damage = (int32_t)((uint32_t)damage * 2u);
    body = (unsigned)world_rnd(&w->rng, 7);
    if (body == 6) body = 3;
    armor = a->events->armor[body];
    if (armor_percent) {
        erosion = (int32_t)((int64_t)armor * armor_percent / 100);
        absorbed = (int32_t)((int64_t)damage * erosion / 100) + armor / 20;
        if (erosion_percent)
            erosion = (int32_t)((int64_t)absorbed * erosion_percent / 100) + 1;
    }
    if (erosion > 0) {
        int16_t remaining = (int16_t)((uint16_t)armor - (uint32_t)erosion);
        a->body_mark_count[body] = 4;
        if (remaining < 0) remaining = 0;
        a->events->armor[body] = remaining;
        if (!remaining) a->events->max_armor[body] = 0;
    }
    damage -= absorbed;
    hp = (int16_t)p->unk_0038;
    if (hp <= 0 || damage <= 0) return 0;
    if (snd_done(a->pc_sound, 0)) {
        int32_t volume = (int32_t)((uint32_t)damage * 1024u + 0x4000u);
        if (volume > 0x7FFF) volume = 0x7FFF;
        a->pc_sound = snd_play(0, volume, 0x8000);
    }
    hp = (int16_t)((uint16_t)hp - (uint32_t)damage);
    if (hp < 0) hp = 0;
    p->unk_0038 = low_word(p->unk_0038, hp);
    return 0;
}
