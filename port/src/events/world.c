#include "events.h"
#include "ai.h"
#include "audio.h"

static int tile_index(const world_object *p, int minimum)
{
    int z = p->z < minimum ? minimum : p->z;
    return (p->x >> 16) + (p->y >> 16) * CM_MAP_SIDE +
        ((p->floor_z + z) >> 16) * CM_MAP_CELLS;
}

static events_event *event_at(events_state *e, int32_t index)
{
    return index >= 0 && (unsigned)index <= e->event_count ? &e->events[index] : NULL;
}

static int event_set(events_state *e, int32_t index, int mode)
{
    events_event *v = event_at(e,index);
    if (!v) return cm_fail("MoveWorld/event",(uint32_t)index,"invalid linked event");
    v->state = 0; v->value[1] = mode < 0 ? v->value[1] ^ 1 : mode; v->value[12] = -1;
    return 0;
}

static void once(events_event *v)
{
    if (v->once) v->enabled = 0;
}

static int actor_object(const world *w, int32_t person)
{
    if (person < 0 || (unsigned)person >= w->person_count) return -1;
    return w->persons[person].object;
}

static int event_tile(const world *w, int32_t person, int player_tile)
{
    int object;
    if (!person) return player_tile;
    object = actor_object(w,person);
    return object >= 0 ? tile_index(&w->objects[object],0) : -1;
}

static int set_enabled(events_state *e, int32_t index, int enable)
{
    events_event *v = event_at(e,index);
    if (!v) return cm_fail("MoveWorld/event",(uint32_t)index,"invalid enable event");
    v->enabled = (uint8_t)enable;
    return 0;
}

static int timer_toggle(events_state *e, sim_state *s, int32_t index)
{
    events_event *v = event_at(e,index);
    if (!v) return cm_fail("MoveWorld/timer",(uint32_t)index,"invalid timer event");
    if (v->state) {
        if (s->bp_limit_flag) --v->state;
        if (!v->state) return event_set(e,index,-1);
    }
    return 0;
}

static int set_texture(world *w, int32_t target, int32_t source)
{
    int object = world_fxo_object(w,(unsigned)target);
    int texture = world_fxo_object(w,(unsigned)source);
    if (object < 0 || texture < 0) return cm_fail("MoveWorld/texture",(uint32_t)target,"invalid FXO texture source");
    w->objects[object].texture = w->objects[texture].texture;
    return 0;
}

static int move_actor(world *w, int object, int32_t x, int32_t y, int32_t z, uint16_t heading)
{
    world_object *p;
    if (object <= 0 || (unsigned)object >= w->object_count) return cm_fail("MoveWorld/person",(uint32_t)object,"invalid actor object");
    p = &w->objects[object];
    if (p->active) world_omap_delete(w,(unsigned)object);
    p->x = x; p->y = y; p->floor_z = z; p->heading = heading;
    if (p->active) world_omap_insert(w,(unsigned)object);
    return 0;
}

static int move_fxo(events_state *e, sim_state *s, ai_state *a, world *w, events_event *v)
{
    int object = world_fxo_object(w,(unsigned)v->value[3]);
    world_object *p;
    int32_t *axis, old, step;
    int increase, reached = 0;
    unsigned i;
    (void)e; (void)a;
    if (object < 0) return cm_fail("MoveWorld/motion",(uint32_t)v->value[3],"invalid moving FXO");
    p = &w->objects[object];
    axis = v->type == 6 ? &p->z : v->type == 7 ? &p->x : &p->y;
    if (!v->state) {
        if (v->value[13] && v->value[12] == -1)
            v->value[12] = snd_object(w,(unsigned)v->value[13],(unsigned)object,0x60000,32767,0);
        old = *axis; step = (int32_t)((int64_t)v->value[4] * s->frame_ticks / 2);
        increase = v->value[1] == v->value[9];
        if (v->type != 6) world_fxo_map_delete(w,(unsigned)object);
        *axis = (int32_t)((uint32_t)*axis + (increase ? (uint32_t)step : 0u - (uint32_t)step));
        if (increase && *axis >= v->value[5]) { *axis = v->value[5]; reached = 1; }
        if (!increase && *axis <= v->value[6]) { *axis = v->value[6]; reached = 1; }
        if (v->type == 6 && (p->flags & 0x400000u)) {
            for (i = 0; i < w->person_count; ++i) {
                int o = w->persons[i].object;
                world_object *person;
                if (o < 0 || (unsigned)o >= w->object_count) continue;
                person = &w->objects[o];
                if (person->x < p->x - p->radius || person->x > p->x + p->radius ||
                    person->y < p->y - p->height || person->y > p->y + p->height) continue;
                if (increase && old <= person->floor_z && person->floor_z <= p->z) person->floor_z = p->z;
                if (!increase && p->z <= person->floor_z && person->floor_z <= old) {
                    int32_t floor = p->z < 0 ? 0 : p->z;
                    if (floor < person->floor_z - 0x4000) floor = person->floor_z - 0x4000;
                    person->floor_z = floor;
                }
            }
        }
        if (v->value[8]) {
            int other = world_fxo_object(w,(unsigned)v->value[8]);
            if (other < 0) return cm_fail("MoveWorld/motion",(uint32_t)v->value[8],"invalid linked FXO");
            w->objects[other].z = p->z + v->value[2];
        }
        if (v->type != 6) {
            world_fxo_map_insert(w,(unsigned)object);
            for (i = 0; i < 4; ++i) {
                if (v->type == 7) p->corner_x[i] = p->x + ((i == 0 || i == 3) ? -p->radius : p->radius);
                else p->corner_y[i] = p->y + (i == 0 || i == 3 ? -p->radius : p->radius);
            }
        }
        if (reached) {
            v->state = (uint8_t)v->value[7];
            if (v->value[14]) (void)snd_object(w,(unsigned)v->value[14],(unsigned)object,0x60000,32767,0);
            if (v->value[12] != -1) { snd_abort(v->value[12],(unsigned)v->value[13]); v->value[12] = -1; }
        }
    }
    if (v->value[11]) {
        unsigned pass = (unsigned)v->value[11];
        if (pass >= 96) return cm_fail("MoveWorld/aperture",pass,"invalid aperture");
        w->apertures[pass].enabled = (uint8_t)(v->value[9] ? *axis >= v->value[10] : *axis <= v->value[10]);
    }
    return 0;
}

static int dispatch(events_state *e, sim_state *s, ai_state *a, world *w, unsigned index, int tile)
{
    events_event *v = &e->events[index], *target;
    int32_t *p = v->value;
    int object, other, n;
    unsigned i;
    target = event_at(e,p[0]);
    switch (v->type) {
    case 0: break;
    case 1:
        if (event_set(e,p[0],-1)) return -1;
        once(v); break;
    case 2: case 21: case 63:
        if ((v->type == 21 && e->switches[p[3]].state != p[9]) ||
            (v->type == 63 && e->switches[p[3]].state != p[9])) break;
        if (event_tile(w,p[v->type == 21 ? 8 : 3],tile) != p[2]) break;
        if (!target) return cm_fail("MoveWorld",index,"invalid position target");
        if (v->type == 2 && p[5] > 0 && w->objects[w->player_object].floor_z < p[5]) break;
        if (target->value[1] != p[1]) {
            if (event_set(e,p[0],p[1])) return -1;
            if (v->type == 2 && ((p[8] && set_enabled(e,p[8],1)) || (p[15] && set_enabled(e,p[15],0)))) return -1;
            once(v);
        }
        break;
    case 3:
        n = event_tile(w,p[3],tile); n = n == p[2] || n == p[6];
        if (!target) return cm_fail("MoveWorld",index,"invalid tile target");
        if (target->value[1] == !n && event_set(e,p[0],n)) return -1;
        break;
    case 4: case 22:
        if ((unsigned)p[3] >= e->switch_count || !target) return cm_fail("MoveWorld",index,"invalid switch target");
        n = e->switches[p[3]].state;
        if ((v->type == 4 ? n != target->value[1] : n == target->value[1]) && event_set(e,p[0],-1)) return -1;
        break;
    case 5: return timer_toggle(e,s,p[0]);
    case 6: case 7: case 8: return move_fxo(e,s,a,w,v);
    case 9:
        if ((unsigned)p[3] >= 32) return cm_fail("MoveWorld/light",index,"invalid light index");
        n = w->light_param[p[3]] + (p[1] ? p[4] : -p[4]);
        if (p[1] && n > p[5]) { n = p[5]; p[1] = 0; }
        else if (!p[1] && n < p[6]) { n = p[6]; p[1] = 1; }
        w->light_param[p[3]] = (int16_t)n; break;
    case 10:
        if ((unsigned)p[3] >= 32) return cm_fail("MoveWorld/light",index,"invalid light index");
        w->light_param[p[3]] = (int16_t)(world_rnd(&w->rng,p[4]) * (uint16_t)p[5] + (uint16_t)p[6]); break;
    case 11: case 14: case 19: case 20:
        if (v->type == 19) {
            if (!target || target->value[1] == p[9]) break;
        } else {
            if ((unsigned)p[3] >= e->switch_count) return cm_fail("MoveWorld/timer",index,"invalid switch index");
            if (e->switches[p[3]].state != p[9]) break;
        }
        if (v->type == 20) {
            if (event_set(e,p[0],-1)) return -1;
            if (events_key_search(e,s,w,0) < 0) return -1;
        } else if (timer_toggle(e,s,p[0])) return -1;
        break;
    case 12:
        if (tile != p[2]) break;
        w->unk_5E7A8 = p[3];
        object = (int)w->player_object;
        {
            world_object *pc = &w->objects[object];
            int x = pc->x & 65535, y = pc->y & 65535;
            if (p[9] != 999) {
                if (w->unk_5E7A8) { pc->z = 0x3800; x = p[4] + x * 60 / 100; y = p[10] + y * 60 / 100; s->unk_144b34 = s->unk_144b34 * 60 / 100; }
                else { pc->z = 0x5D55; x = 0x8000 + (x-p[4])*10/6; y = 0x8000+(y-p[10])*10/6; s->unk_144b34 = s->unk_144b34*10/6; }
            }
            if (p[8]) pc->floor_z = (int32_t)((uint32_t)p[11] << 16);
            if (x < 0) x = 0;
            if (x > 65535) x = 65535;
            if (y < 0) y = 0;
            if (y > 65535) y = 65535;
            s->body_height = pc->z + pc->z/2 + 0x1000 + s->unk_144b34;
            if (pc->active && move_actor(w,object,(p[6]&~65535)+x,(p[5]&~65535)+y,pc->floor_z,pc->heading)) return -1;
        }
        break;
    case 13:
        object = world_fxo_object(w,(unsigned)p[3]);
        if (object < 0) return cm_fail("MoveWorld/linkflat",index,"invalid FXO");
        for (i = 4; i < 15; ++i) if (p[i] > 0) {
            other = world_fxo_object(w,(unsigned)p[i]);
            if (other < 0) return cm_fail("MoveWorld/linkflat",index,"invalid linked FXO");
            w->objects[other].z = w->objects[object].z + p[2];
        }
        break;
    case 15:
        if (p[1] || (unsigned)p[8] >= e->switch_count || e->switches[p[8]].state != p[9]) break;
        if (p[3] < 0 || (unsigned)p[3] >= w->prop_count) return cm_fail("MoveWorld/prop",index,"invalid prop");
        object = w->prop_objects[p[3]];
        if (object) { world_omap_delete(w,(unsigned)object); w->objects[object].active = 1;
            w->objects[object].x = p[6]; w->objects[object].y = p[5]; world_omap_insert(w,(unsigned)object); p[1] = 1; }
        once(v); break;
    case 16:
        if (v->state) break;
        for (i = 3; i < 15; ++i) if (p[i] > 0) {
            if ((unsigned)p[i] >= w->person_count) return cm_fail("MoveWorld/person",index,"invalid actor state target");
            {
                world_person *person = &w->persons[p[i]];
                static const int16_t states[5] = {1,10,3,0,0};
                n = p[0] >= 0 && p[0] <= 4 ? states[p[0]] : 1;
                if (p[0] < 3 ? person->state == 9 : person->status != 2) {
                    person->state = (int16_t)n; person->unk_001E = n ? 0 : 30000;
                }
            }
        }
        v->state = 1; once(v); break;
    case 17:
        if (v->state) break;
        if (p[1]) {
            e->text_message = p[0]; e->text_trigger = p[7]; e->text_object = 0;
            e->text_switch1 = p[13]; e->text_switch0 = p[14]; e->text_event = p[15];
            if (p[2] > 0 && p[2] < CM_MAP_CELLS*CM_MAP_LEVELS) w->cells[p[2]/CM_MAP_CELLS][p[2]%CM_MAP_CELLS].light = (uint8_t)p[3];
            if (p[11]) {
                if ((unsigned)p[11] >= w->prop_count) return cm_fail("MoveWorld/text",index,"invalid text prop");
                e->text_object = w->prop_objects[p[11]];
                w->objects[e->text_object].active = 1;
                if (move_actor(w,e->text_object,p[6],p[5],w->objects[e->text_object].floor_z,w->objects[e->text_object].heading)) return -1;
            }
            if (p[9]) { object = actor_object(w,p[9]); if (object < 0) return cm_fail("MoveWorld/text",index,"invalid speaker");
                e->text_object = object;
                if (!p[6] && w->persons[p[9]].status != 2) w->persons[p[9]].state = 3;
            }
            if (snd_message(w,w->scene,(unsigned)e->text_message,e->text_object)) return -1;
        } else {
            e->text_trigger = 0; snd_message_stop();
            if (p[2] > 0 && p[2] < CM_MAP_CELLS*CM_MAP_LEVELS) w->cells[p[2]/CM_MAP_CELLS][p[2]%CM_MAP_CELLS].light = (uint8_t)p[8];
            if (p[11]) {
                if ((unsigned)p[11] >= w->prop_count) return cm_fail("MoveWorld/text",index,"invalid text prop");
                object = w->prop_objects[p[11]]; w->objects[object].active = 1;
                if (move_actor(w,object,p[4],p[10],w->objects[object].floor_z,w->objects[object].heading)) return -1;
            }
        }
        v->state = 1; break;
    case 18:
        if (tile != p[2] || !target) break;
        if (!v->state && target->value[1] != p[1]) { if (event_set(e,p[0],p[1])) return -1; v->state = (uint8_t)p[7]; }
        else if (v->state && s->bp_limit_flag) --v->state;
        break;
    case 23:
        if (v->state) { if (s->bp_limit_flag) --v->state; break; }
        v->state = (uint8_t)p[7]; ++p[0]; if (p[0] >= p[5]) p[0] = 0;
        return set_texture(w,p[3],p[0]+p[8]);
    case 24:
        if (!target) return cm_fail("MoveWorld/event",index,"invalid relay target");
        if (!target->state && p[1] != target->value[1]) { p[1] = target->value[1]; if (event_set(e,p[3],-1)) return -1; }
        break;
    default: return cm_fail("MoveWorld/type",v->type,"event type not implemented");
    }
    return 0;
}

int events_step(events_state *e, sim_state *s, ai_state *a, world *w)
{
    unsigned i, event, hops;
    int tile;
    if (!e || !s || !a || !w || w->player_object >= w->object_count) return cm_fail("MoveWorld",0,"missing simulation state");
    e->ticks = s->timer_ticks;
    if (e->selector_kind) return events_process_selector(e,s,w);
    for (i = 0; i < 3; ++i) if (s->kb_scan_status[0x3B+i]) {
        s->kb_scan_status[0x3B+i] = 0; e->previous_mode = s->pc_mode;
        if (events_make_selector(e,i+1)) return -1;
        s->pc_mode = 3; return 0;
    }
    if (s->kb_scan_status[0x1C]) {
        s->kb_scan_status[0x1C] = 0;
        if (s->pc_mode == 7) { if (events_set_tank_mode(e,s,w,0)) return -1; }
        else if (events_switch_search(e,s,a,w)) return -1;
    }
    if (e->tank_flag && s->pc_mode != 7 && events_set_tank_mode(e,s,w,1)) return -1;
    tile = tile_index(&w->objects[w->player_object],0x5000);
    if (tile < 0 || tile >= CM_MAP_LEVELS*CM_MAP_CELLS) return cm_fail("MoveWorld/tile",(uint32_t)tile,"player tile outside map");
    i = w->cells[tile/CM_MAP_CELLS][tile%CM_MAP_CELLS].unk_C5090;
    if (i >= 64) return cm_fail("MoveWorld/region",i,"region outside chain heads");
    event = e->event_heads[i]; hops = 0;
    while (event) {
        events_event *v;
        if (event > e->event_count || ++hops > e->event_count) return cm_fail("MoveWorld/chain",event,"invalid/cyclic event chain");
        v = &e->events[event];
        if (v->enabled && dispatch(e,s,a,w,event,tile)) return -1;
        event = v->next;
    }
    return 0;
}
