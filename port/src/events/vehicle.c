#include "events.h"
#include "ai.h"

/* CM_HI.EXE SwitchSearch 01AF9C and vehiclePERSON 0962BC. */
static const uint16_t vehicle_person[12][4] = {
    {0,0,0,0},{84,0,0,0},{0,0,0,0},{20,195,0,0},
    {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
    {0,0,0,0},{11,0,0,0},{0,0,0,0},{55,41,0,0}
};

static int angle_difference(uint16_t a, uint16_t b)
{
    int d = (int16_t)(uint16_t)(a - b);
    return d < 0 ? -d : d;
}

static int switch_object(const world *w, const events_switch *v)
{
    return world_fxo_object(w, (unsigned)v->fxo);
}

int events_toggle_switch(events_state *e, world *w, unsigned index)
{
    events_switch *v;
    int object;
    if (index >= e->switch_count) return cm_fail("switch",index,"invalid switch index");
    v = &e->switches[index];
    object = switch_object(w,v);
    if (object < 0) return cm_fail("switch", index, "invalid switch object");
    v->state ^= 1;
    w->objects[object].texture = v->textures[v->state];
    if (v->link > 0) {
        events_switch *other = &e->switches[v->link];
        object = switch_object(w, other);
        if (object < 0) return cm_fail("switch", (uint32_t)v->link, "invalid linked switch object");
        other->state = v->state;
        if (object) w->objects[object].texture = other->textures[other->state];
    }
    return 0;
}

int events_key_search(events_state *e, sim_state *s, world *w, unsigned key)
{
    const world_object *p = &w->objects[w->player_object];
    int32_t tile = (p->x >> 16) + (p->y >> 16) * CM_MAP_SIDE +
        (world_add32(p->floor_z,p->z) >> 16) * CM_MAP_CELLS;
    unsigned i;
    for (i = 0; i < e->switch_count; ++i) {
        events_switch *v = &e->switches[i];
        const world_object *o;
        int object;
        if (v->tile != tile || v->key != (int)key ||
            (v->link < 0 && !e->switches[-v->link].state)) continue;
        object = switch_object(w,v);
        if (object < 0) return cm_fail("UseItem/key",i,"invalid switch object");
        o = &w->objects[object];
        if ((int64_t)p->floor_z + 0x3000 + s->unk_144b34 >= o->z ||
            (int64_t)p->floor_z + p->z + p->height <= o->z ||
            cm_distance(p->x,p->y,o->x,o->y) >= 0x8C00 ||
            angle_difference(p->heading,cm_atan64k(p->x,p->y,o->x,o->y)) >= 0x1000) continue;
        if (events_toggle_switch(e,w,i)) return -1;
        return 1;
    }
    return 0;
}

int events_switch_search(events_state *e, sim_state *s, ai_state *a, world *w)
{
    const world_object *p;
    unsigned i;
    int32_t tile;
    if (!e || !s || !w || !a || w->player_object >= w->object_count)
        return cm_fail("switch", 0, "invalid player state");
    p = &w->objects[w->player_object];
    tile = (p->x >> 16) + (p->y >> 16) * CM_MAP_SIDE + (world_add32(p->floor_z,p->z) >> 16) * CM_MAP_CELLS;
    for (i = 0; i < e->switch_count; ++i) {
        const events_switch *v = &e->switches[i];
        const world_object *o;
        int object, have_key;
        unsigned j;
        if (v->tile != tile || (v->link < 0 && !e->switches[-v->link].state)) continue;
        object = switch_object(w, v);
        if (object < 0) return cm_fail("switch", i, "invalid FX object");
        o = &w->objects[object];
        if ((int64_t)p->floor_z + 0x3000 + s->unk_144b34 >= o->z ||
            (int64_t)p->floor_z + p->z + p->height <= o->z ||
            cm_distance(p->x, p->y, o->x, o->y) >= 0x8C00 ||
            angle_difference(p->heading, cm_atan64k(p->x,p->y,o->x,o->y)) >= 0x1000) continue;
        have_key = !v->key;
        if (v->key && v->enabled)
            for (j = 0; j < EVENTS_ITEM_MAX; ++j)
                if (e->items[j].type > 0 && e->items[j].kind == 14 && e->items[j].subtype == v->key) have_key = 1;
        if (!have_key) continue;
        return events_toggle_switch(e,w,i);
    }
    if (e->tank_flag || w->scene >= 12) return 0;
    for (i = 0; i < 4; ++i) {
        unsigned person = vehicle_person[w->scene][i];
        const world_object *v;
        int object, kind;
        e->vehicle_person = (int32_t)person;
        if (!person) return 0;
        if (person >= w->person_count) return cm_fail("vehiclePERSON", person, "vehicle person not loaded");
        object = w->persons[person].object;
        if (object <= 0 || (unsigned)object >= w->object_count) return cm_fail("vehiclePERSON", person, "invalid vehicle object");
        v = &w->objects[object];
        if (cm_distance(p->x,p->y,v->x,v->y) >= 0x7000 ||
            (int64_t)v->floor_z + v->z <= p->floor_z ||
            (int64_t)p->floor_z + p->z + p->height <= (int64_t)v->floor_z + v->z ||
            angle_difference(p->heading,cm_atan64k(p->x,p->y,v->x,v->y)) >= 0x1800) continue;
        kind = ai_person_kind(a,w,person);
        if (kind != 5 && kind != 17) continue;
        e->vehicle_type = kind == 17; e->tank_flag = 1;
        return 0;
    }
    return 0;
}

/* ClearVehicle 046A78 preserves heading, clears floor search, then ClearObject. */
int events_clear_vehicle(sim_state *s, world *w, unsigned object)
{
    if (!s || !w || object >= w->object_count) return cm_fail("vehicle", object, "invalid exit object");
    s->no_openings = 0; s->unk_130d18 = 0; s->save_pc_vector = w->objects[object].heading;
    s->collision_level = world_add32(w->objects[object].floor_z,0x4800) >> 16;
    if (s->collision_level > w->top_level) s->collision_level = w->top_level;
    world_omap_delete(w,object);
    if (sim_clear_object(s,w,object)) return -1;
    world_omap_insert(w,object);
    return 0;
}

/* SetTankMode 012794; OS memory and viewport work belongs to UI/render. */
int events_set_tank_mode(events_state *e, sim_state *s, world *w, int enter)
{
    world_object *p;
    unsigned object;
    if (!e || !s || !w || w->player_object >= w->object_count)
        return cm_fail("vehicle", 0, "invalid player");
    e->tank_flag = 0;
    s->tank_hand=0; /* SetTankMode0127A3 clears the secondary-hand global. */
    if (enter) {
        if (e->vehicle_person <= 0 || (unsigned)e->vehicle_person >= w->person_count)
            return cm_fail("vehicle", (uint32_t)e->vehicle_person, "invalid vehicle person");
        object = (unsigned)w->persons[e->vehicle_person].object;
        if (!object || object >= w->object_count) return cm_fail("vehicle", object, "invalid vehicle object");
        if (s->pc_mode != 3) s->previous_pc_mode = s->pc_mode;
        e->foot_object = w->player_object; e->foot_person = w->player_person;
        world_omap_delete(w,w->player_object);
        p = &w->objects[w->player_object]; p->x = p->y = 0x1F8000; p->active = 0;
        w->player_person = (unsigned)e->vehicle_person; w->player_object = object;
        s->pc_mode = 7; s->vehicle_type = e->vehicle_type;
        if (s->vehicle_type) { s->flight_mode = 2; s->flight_active = 1; s->flight_unk_144B48 = 0; }
    } else {
        const world_object *old = &w->objects[w->player_object];
        object = e->foot_object;
        if (!object || object >= w->object_count) return cm_fail("vehicle", object, "invalid on-foot object");
        w->player_object = object; w->player_person = e->foot_person;
        p = &w->objects[object]; p->x = old->x; p->y = old->y; p->floor_z = old->floor_z;
        p->heading = old->heading; p->active = 1;
        s->pc_mode = s->previous_pc_mode; s->flight_mode = 0;
        world_omap_insert(w,object);
        if (events_clear_vehicle(s,w,object)) return -1;
    }
    p = &w->objects[w->player_object];
    s->body_height = world_add32(world_add32(p->z,p->z/2),0x1000);
    s->unk_144b34 = s->unk_144b38 = s->unk_144b3c = 0;
    w->camera.pitch = 0;
    return 0;
}
