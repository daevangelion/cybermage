#include "world_internal.h"
#include <string.h>

static int object_slot(world *w, world_object **out)
{
    if (w->object_count >= 2378) {
        (void)cm_fail("SCENE.RSC", w->object_count, "persistent object slots exhausted");
        return -1;
    }
    *out = &w->objects[w->object_count];
    return 0;
}
static unsigned scale_for(const uint8_t *p)
{
    unsigned percent = cm_u16(p);
    return (percent ? percent : 100) * 512u / 100u;
}

/* LoadFXO 0x25CE4: placements30, definitions16 in section8/index0. */
static int load_fxo(world *w)
{
    static const uint32_t flags[11] = {
        0x08000000,0x48000000,0x0C000000,0x02C40000,0x42440000,
        0x02800000,0x02440000,0x48200000,0x48100000,0x08004000,0x08200000
    };
    unsigned count, i;
    world_span span = w->scene_sections[2];
    if (world_section_records(span, 30, &count)) return -1;
    for (i = 0; i < count; ++i) {
        const uint8_t *p = span.data + (i + 1) * 30;
        int type = world_s16(p + 24);
        const uint8_t *def;
        world_object *o;
        unsigned tex, kind, j, id;
        int32_t dx, dy;
        if (type <= 0) continue;
        if ((unsigned)type >= w->fxo_definitions.size / 16 || w->fxo_count == 2048)
            return cm_fail("SCENE.RSC/FXO", i, "invalid FX object definition");
        if (object_slot(w, &o)) return -1;
        def = w->fxo_definitions.data + (unsigned)type * 16;
        kind = cm_u32(def + 8);
        o->flags = kind <= 10 ? flags[kind] : 0x08000000u;
        o->x = world_s32(p); o->y = world_s32(p + 4); o->z = world_s32(p + 8);
        o->heading = cm_u16(p + 12); o->active = p[28]; o->kind = 0;
        o->radius = world_s32(p + 16) ? world_s32(p + 16) : world_s32(def);
        o->height = world_s32(p + 20) ? world_s32(p + 20) : world_s32(def + 4);
        o->unk_6F220 = world_sub32(0,o->height); o->unk_71E88 = o->height;
        if (world_s16(def + 12) <= 0)
            return cm_fail("SCENE.RSC/FXO", i, "invalid FX image resource");
        if (world_texture_package(w, WORLD_TEX_FXO, cm_u16(def + 12), 0,
                                  scale_for(def + 14), &tex)) return -1;
        w->fxo_textures[w->fxo_count] = (uint16_t)tex;
        o->texture = (uint16_t)tex; o->rotations = 1;
        if (cm_u16(p + 26)) {
            o->texture = cm_u16(p + 26);
            for (id = 0; id < 256 && w->map_id_texture[id] != o->texture; ++id) {}
            if (id == 256) return cm_fail("SCENE.RSC/FXO", i, "unknown overridden panel texture");
            o->flags = (o->flags & ~0x00180000u) | (w->map_id_flags[id] & 0x00180000u);
        }
        j = (uint16_t)(o->heading + 0x4000u) >> 5;
        dx = world_mul16(o->radius, w->trig.cos[j]);
        dy = world_mul16(o->radius, w->trig.sin[j]);
        o->corner_x[0] = o->corner_x[3] = world_sub32(o->x,dx);
        o->corner_x[1] = o->corner_x[2] = world_add32(o->x,dx);
        o->corner_y[0] = o->corner_y[3] = world_sub32(o->y,dy);
        o->corner_y[1] = o->corner_y[2] = world_add32(o->y,dy);
        o->person = (int16_t)w->fxo_count; o->targetable = 3;
        w->fxo_objects[w->fxo_count] = (uint16_t)w->object_count++;
        w->fxo_types[w->fxo_count++] = (int16_t)type;
    }
    return 0;
}

/* LoadPROPS 0x2449C: placements30, definitions0x41B in section8/index1. */
static int load_props(world *w)
{
    world_span span = w->scene_sections[1];
    unsigned count, i;
    if (world_section_records(span, 30, &count)) return -1;
    for (i = 0; i < count; ++i) {
        const uint8_t *p = span.data + (i + 1) * 30, *def;
        int type = world_s16(p + 20), degrees;
        world_object *o;
        unsigned tex, packed;
        if (type <= 0) continue;
        if ((unsigned)type >= w->prop_definitions.size / 1051 || w->prop_count == 2048)
            return cm_fail("SCENE.RSC/PROP", i, "invalid prop definition");
        if (object_slot(w, &o)) return -1;
        def = w->prop_definitions.data + (unsigned)type * 1051;
        o->flags = 0x60000800;
        packed = cm_u32(def + 20) != 0;
        if (packed) o->flags |= 0x80000u;
        o->x = world_add32(world_s32(p),world_s32(def));
        o->y = world_add32(world_s32(p+4),world_s32(def+4));
        o->z = world_add32(world_s32(p+8),world_s32(def+8));
        o->floor_z = world_s32(p + 16);
        o->radius = world_s32(def + 12); o->height = world_s32(def + 16);
        o->unk_6F220 = world_sub32(0,o->height); o->unk_71E88 = o->height;
        degrees = world_s32(p + 12);
        o->heading = world_s16(p + 26) > 0 ? cm_u16(p + 12) :
                     (uint16_t)(world_wrap32((uint32_t)degrees * 65536u) / 360);
        o->active = p[28]; o->kind = 2;
        if (world_prop_texture(w, def, &tex)) return -1;
        o->texture = (uint16_t)tex; o->rotations = (int8_t)w->textures[tex].rotations;
        o->person = (int16_t)w->prop_count; o->targetable = 2;
        if (type == 9 || (type >= 103 && type <= 110)) {
            o->unk_74AF0 = 0xC00;
            if (w->scene == 7 && (o->x < 0xA0000 || o->y < 0x130000)) o->flags |= 0x10000u;
            if (type >= 108 && type <= 110) o->flags |= 0x01000000u;
        }
        if (type == 174) o->flags |= 0x400u;
        w->prop_objects[w->prop_count] = (uint16_t)w->object_count++;
        w->prop_types[w->prop_count++] = (int16_t)type;
    }
    return 0;
}

int world_load_placements(world *w)
{
    return load_fxo(w) || load_props(w) ? -1 : 0;
}

/* SetPERSON0x21B9C/LoadPERSONS share the same body/animation setup.
 * AI owns the separate HP/loot/difficulty initialization and RNG draws. */
int world_set_person(world *w, unsigned index, unsigned type, unsigned state,
                      int32_t x, int32_t y, int32_t z, int32_t floor_z,
                      uint16_t heading)
{
    static const int16_t states[5] = {0,9,10,8,13};
    static const uint8_t animations[5] = {5,1,1,29,0};
    const uint8_t *mon, *g, *seq;
    world_person *person;
    world_object *o;
    int gsx, seqid;
    unsigned cursor, frame, image, j;
    if (index >= w->person_count || type >= w->monster_definitions.size / 276)
        return cm_fail("world/PERSON", index, "invalid person/monster index");
    person = &w->persons[index];
    if (person->object <= 0 || (unsigned)person->object >= w->object_count)
        return cm_fail("world/PERSON", index, "invalid person object");
    o = &w->objects[(unsigned)person->object];
    mon = w->monster_definitions.data + type * 276;
    gsx = world_s16(mon + 0x17);
    if (gsx < 0 || world_load_gsx_flags(w, (unsigned)gsx, mon[0x113] & 6u)) return -1;
    g = w->gsx_definitions.data + (unsigned)gsx * 58;
    seqid = world_s16(g);
    if (seqid < 0 || (unsigned)seqid >= w->animation_definitions.size / 4532)
        return cm_fail("world/PERSON", index, "invalid animation object");
    seq = w->animation_definitions.data + (unsigned)seqid * 4532;
    for (j = 0; j < w->loaded_monster_count && w->loaded_monster_types[j] != (int)type; ++j) {}
    if (j == w->loaded_monster_count) {
        if (j >= 109) return cm_fail("world/PERSON", type, "monster type slots exhausted");
        w->loaded_monster_types[w->loaded_monster_count++] = (int16_t)type;
    }
    person->monster_index = j;
    person->type = person->unk_0002 = (int16_t)type;
    if (state == 99) {
        person->state = 2; person->unk_00BC = 1; person->unk_001E = 10;
    } else {
        state = state <= 4 ? state : 0;
        person->state = states[state]; person->unk_00BC = animations[state];
        person->unk_001E = state == 2;
    }
    person->status = state == 3 ? 2 : 0;
    person->unk_0014 = person->unk_0018 = person->unk_001A = person->unk_0020 = 0;
    person->unk_0016 = 1;
    person->target = person->unk_004C = person->unk_0050 = -1;
    person->distance = 0;
    cursor = seq[0x16 + person->unk_00BC];
    if (cursor >= 40 || (frame = seq[0x34 + cursor]) >= 20)
        return cm_fail("world/PERSON", index, "invalid initial animation");
    person->unk_00BD = (uint8_t)cursor; person->unk_00BE = seq[0x5C + cursor];
    o->flags = 0x60080000;
    o->x = world_add32(x,world_s32(seq)); o->y = world_add32(y,world_s32(seq+4));
    o->z = world_add32(z,world_s32(seq+8)); o->floor_z = floor_z; o->heading = heading;
    o->active = 1; o->rotations = 8; o->kind = 2;
    o->person = (int16_t)index; o->targetable = 1;
    o->radius = world_s32(seq + 0x84 + frame * 4);
    o->height = world_s32(seq + 0xD4 + frame * 4);
    o->unk_6F220 = world_s32(seq + 0x124 + frame * 4);
    o->unk_71E88 = world_s32(seq + 0x174 + frame * 4);
    if (person->status == 2 && (mon[0x113] & 2)) o->rotations = 1;
    if (mon[0x113] & 0x10) o->rotations = -9;
    if (mon[0x113] & 8) o->flags |= 0x8000u;
    image = seq[0x264 + frame];
    return world_anim_frame(w, (unsigned)person->object, (unsigned)gsx, image);
}

int world_load_persons(world *w)
{
    unsigned count, i;
    world_span span = w->scene_sections[3];
    if (world_section_records(span, 351, &count)) return -1;
    for (i = 0; i < count; ++i) {
        const uint8_t *p = span.data + (i + 1) * 351;
        int type = world_s16(p + 345);
        world_person *person;
        world_object *o;
        if (type <= 0) continue;
        if ((unsigned)type >= w->monster_definitions.size / 276)
            return cm_fail("SCENE.RSC/PERSON", i, "invalid monster definition");
        if (object_slot(w, &o)) return -1;
        person = &w->persons[w->person_count];
        person->object = (int16_t)w->object_count;
        person->placement_index = i;
        ++w->object_count; ++w->person_count;
        if (world_set_person(w,w->person_count-1,(unsigned)type,p[1],
                             world_s32(p+319),world_s32(p+323),world_s32(p+327),
                             world_s32(p+347),(uint16_t)(world_wrap32((uint32_t)world_s32(p+331)*65536u)/360)))
            return -1;
        if (p[0x123] & 1) o->flags |= 0x20000u;
        if (p[0x123] & 0x20) o->flags |= 0x800u;
    }
    if (!w->person_count) return cm_fail("SCENE.RSC/PERSON", 0, "scene has no player");
    w->player_person = 0; w->player_object = (unsigned)w->persons[0].object;
    w->camera.u = w->objects[w->player_object].x;
    w->camera.v = w->objects[w->player_object].y;
    w->camera.z = world_add32(w->objects[w->player_object].floor_z,w->objects[w->player_object].z);
    w->camera.angle = w->objects[w->player_object].heading;
    w->camera.horizon = 150;
    return 0;
}
