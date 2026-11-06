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
    int object;
    if (person < 0 || (unsigned)person >= w->person_count) return -1;
    object=w->persons[person].object;
    return object>=0 && (unsigned)object<w->object_count?object:-1;
}

static int event_tile(const world *w, int32_t person, int player_tile)
{
    int object;
    if (!person) return player_tile;
    object = actor_object(w,person);
    return object >= 0 ? tile_index(&w->objects[object],INT32_MIN) : -1;
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
    if (object < 0 || source<0 || (unsigned)source>=w->fxo_count)
        return cm_fail("MoveWorld/texture",(uint32_t)target,"invalid FXO texture source");
    w->objects[object].texture = w->fxo_textures[source];
    return 0;
}

static int start_text(events_state *e, world *w, unsigned scene,
                       int32_t message, int object, int trigger)
{
    e->text_message=message;e->text_object=object;e->text_trigger=trigger;
    e->text_switch1=e->text_switch0=e->text_event=0;
    if(snd_message(w,scene,(unsigned)message,object)) return -1;
    e->text_serial=snd_text()->revision;
    return 0;
}

/* SpeechMessage017E9A completion changes only the named switch, not its link. */
static int dialogue_tick(events_state *e, world *w)
{
    unsigned i;
    const snd_dialogue *text=snd_text();
    if(!e->text_trigger) return 0;
    if(!e->text_serial) {
        if(snd_message(w,e->text_trigger==10?9:w->scene,(unsigned)e->text_message,e->text_object)) return -1;
        e->text_serial=snd_text()->revision;
        return 0;
    }
    if(text->active) return 0;
    for(i=0;i<2;++i) {
        int index=i?e->text_switch0:e->text_switch1;
        if(index) {
            events_switch *v;
            int object;
            if(index<0 || (unsigned)index>=e->switch_count) return cm_fail("SpeechMessage",(uint32_t)index,"invalid completion switch");
            v=&e->switches[index];object=world_fxo_object(w,(unsigned)v->fxo);
            if(object<0) return -1;
            v->state=(uint8_t)!i;w->objects[object].texture=v->textures[v->state];
        }
    }
    if(e->text_event) {
        if(set_enabled(e,e->text_event,1) || event_set(e,e->text_event,-1)) return -1;
    }
    e->text_trigger=e->text_object=0;e->text_serial=0;
    return 0;
}

static int talk_texture(events_state *e, sim_state *s, world *w, events_event *v,
                         int per_frame)
{
    int32_t *p=v->value;
    int object=world_fxo_object(w,(unsigned)p[3]);
    if(object<0) return -1;
    if(e->text_trigger && e->text_object==object) {
        if(per_frame) { if(p[13]) { --p[13];return 0; }p[13]=p[7]; }
        else { if(v->state) { if(s->bp_limit_flag) --v->state;return 0; }v->state=(uint8_t)p[7]; }
        ++p[15];if(p[15]>=p[14]) p[15]=0;
        return set_texture(w,p[3],p[3]+p[15]);
    }
    return set_texture(w,p[3],p[3]+p[11]);
}

/* MoveWorld type60, original03D4C9..03D93B. Numeric tables are read from LE. */
static int monitor(events_state *e, world *w, events_event *v)
{
    int32_t *p=v->value;
    const world_object *pc=&w->objects[w->player_object];
    int object=world_fxo_object(w,(unsigned)p[3]);
    unsigned type,step,frame,count,i;
    if(!e->tables_loaded) return cm_fail("MoveWorld/video",0,"monitor tables not loaded");
    if(object<0) return -1;
    if(pc->x<=p[6] || pc->x>=p[4] || pc->y<=p[5] || pc->y>=p[10] ||
       ((pc->floor_z+pc->z)>>16)!=(w->objects[object].z>>16)) {
        p[15]=p[11]=0;return 0;
    }
    if(!p[15]) {
        int start=1;
        if(!e->text_trigger) {
            unsigned list;
            if(p[9]<0 || p[9]>=9) return cm_fail("MoveWorld/video",(uint32_t)p[9],"invalid video list index");
            list=(unsigned)e->video_list[p[9]];
            if(list>=8) return cm_fail("MoveWorld/video",list,"invalid monitor message");
            p[0]=e->video_flick[list];
            ++p[9];if(p[9]>=9 || !e->video_list[p[9]]) p[9]=0;
            if(start_text(e,w,9,e->video_text[list],object,10)) return -1;
            e->video_chain=0;
        } else if(p[11]>0) { p[0]=p[11]-1;p[11]=0; }
        else start=0;
        if(start) {
            type=(unsigned)p[0];
            if(type>=5) return cm_fail("MoveWorld/video",type,"invalid monitor type");
            count=(unsigned)e->video_frames[type];
            if(count>w->fxo_count || p[3]<0 || (unsigned)p[3]>w->fxo_count-count)
                return cm_fail("MoveWorld/video",count,"monitor frame placements outside scene");
            for(i=0;i<count;++i) {
                int dest=world_fxo_object(w,(unsigned)p[3]+i);
                if(dest<0 || world_load_fxo_news(w,(unsigned)e->video_fxo[type]+i,(unsigned)p[3]+i,(unsigned)dest)) return -1;
            }
            p[8]=p[7]=0;p[15]=1;
        }
    }
    if(e->ticks<=(uint32_t)p[7]) return 0;
    type=(unsigned)p[0];step=(unsigned)p[8];
    if(type>=5 || step>=14) return cm_fail("MoveWorld/video",type,"monitor sequence outside table");
    frame=(unsigned)e->video_seq[type*14+step];
    if((int32_t)frame<0) {
        if(!e->text_trigger) { p[15]=0;return 0; }
        if(e->video_chain) { p[15]=0;p[11]=e->video_chain;e->video_chain=0;return 0; }
        if((int32_t)frame>=-1) return 0;
        p[8]=0;frame=(unsigned)e->video_seq[type*14];
    }
    if(frame>=4) return cm_fail("MoveWorld/video",frame,"invalid monitor frame");
    p[7]=(int32_t)(e->ticks+(uint32_t)e->video_time[type*4+frame]);
    if(set_texture(w,p[3],p[3]+(int32_t)frame)) return -1;
    for(i=12;i<15;++i) if(p[i]>0 && set_texture(w,p[i],p[3]+(int32_t)frame)) return -1;
    ++p[8];return 0;
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

static int move_plane(sim_state *s, ai_state *a, world *w, unsigned object,
                       int32_t z, int rising, int crush)
{
    world_object *o=&w->objects[object];
    int32_t old=o->z;
    o->z=z;
    if(o->flags&0x400000u) {
        if(rising) {
            events_zflat_raise(w,object,old);
            return crush?events_zflat_ucrash(s,a,w,object,old):0;
        }
        events_zflat_drop(w,object,old);
    } else if(crush && !rising) {
        if(o->flags&0x08000000u) return events_zwall_dcrash(s,a,w,object);
        if(o->flags&0x800000u) return events_zflat_dcrash(s,a,w,object);
    }
    return 0;
}

static int move_planes(events_state *e, sim_state *s, ai_state *a, world *w,
                        events_event *v, unsigned object, int32_t z,
                        int rising, int crush)
{
    int result,hit=0,other;
    unsigned i;
    events_event *group;
    result=move_plane(s,a,w,object,z,rising,crush);
    if(result<0) return -1;
    hit|=result;
    if(v->value[8]) {
        other=world_fxo_object(w,(unsigned)v->value[8]);
        if(other<0) return -1;
        result=move_plane(s,a,w,(unsigned)other,z+v->value[2],rising,crush);
        if(result<0) return -1;
        hit|=result;
    }
    i=(unsigned)v->value[rising?15:0];
    if(!i) return hit;
    group=event_at(e,(int32_t)i);
    if(!group) return cm_fail("MoveWorld/group",i,"invalid plane group");
    for(i=3;i<15;++i) if(group->value[i]>0) {
        other=world_fxo_object(w,(unsigned)group->value[i]);
        if(other<0) return -1;
        if((unsigned)other==object) continue;
        result=move_plane(s,a,w,(unsigned)other,z+group->value[2],rising,crush);
        if(result<0) return -1;
        hit|=result;
    }
    return hit;
}

static void motion_start(events_event *v, const world *w, unsigned object)
{
    if(v->value[13] && v->value[12]==-1)
        v->value[12]=snd_object(w,(unsigned)v->value[13],object,0x60000,32767,0);
}
static void motion_stop(events_event *v, const world *w, unsigned object)
{
    v->state=(uint8_t)v->value[7];
    if(v->value[14]) (void)snd_object(w,(unsigned)v->value[14],object,0x60000,32767,0);
    if(v->value[12]!=-1) { snd_abort(v->value[12],(unsigned)v->value[13]);v->value[12]=-1; }
}

static int rotate_fxo(world *w, sim_state *s, events_event *v)
{
    int32_t *p=v->value;
    int object=world_fxo_object(w,(unsigned)p[3]);
    world_object *o;
    unsigned angle,trig;
    int32_t dx,dy;
    int increase,reached=0;
    if(object<0) return -1;
    o=&w->objects[object];angle=o->heading;
    if(!v->state) {
        motion_start(v,w,(unsigned)object);world_fxo_map_delete(w,(unsigned)object);
        increase=p[1]==p[9];
        angle=(uint16_t)(angle+(increase?1:-1)*(int32_t)((int64_t)p[4]*s->frame_ticks/2));
        if(increase && !p[5] && angle<0x1000) angle=0;
        if(!increase && !p[6] && angle>0xF000) angle=0;
        if(increase && angle>=(unsigned)p[5]) { angle=(uint16_t)p[5];reached=1; }
        if(!increase && angle<=(unsigned)p[6]) { angle=(uint16_t)p[6];reached=1; }
        o->heading=(uint16_t)angle;
        trig=(uint16_t)(angle+(p[2]?-0x4000:0x4000))>>5;
        dx=(int32_t)((int64_t)o->radius*w->trig.cos[trig]>>16);
        dy=(int32_t)((int64_t)o->radius*w->trig.sin[trig]>>16);
        if(p[8]) {
            o->x=o->corner_x[1]+dx;o->y=o->corner_y[1]+dy;
            o->corner_x[0]=o->corner_x[1]+dx*2;o->corner_y[0]=o->corner_y[1]+dy*2;
            o->corner_x[3]=o->corner_x[0];o->corner_y[3]=o->corner_y[0];
        } else {
            o->x=o->corner_x[0]+dx;o->y=o->corner_y[0]+dy;
            o->corner_x[1]=o->corner_x[0]+dx*2;o->corner_y[1]=o->corner_y[0]+dy*2;
            o->corner_x[2]=o->corner_x[1];o->corner_y[2]=o->corner_y[1];
        }
        if(reached) motion_stop(v,w,(unsigned)object);
        world_fxo_map_insert(w,(unsigned)object);
    }
    if(p[11]) {
        if((unsigned)p[11]>=96) return cm_fail("MoveWorld/rotate",(uint32_t)p[11],"invalid aperture");
        w->apertures[p[11]].enabled=(uint8_t)(p[9]?angle>=(unsigned)p[10]:angle<=(unsigned)p[10]);
    }
    return 0;
}

static int ycrush_fxo(sim_state *s, ai_state *a, world *w, events_event *v)
{
    int32_t *p=v->value,step;
    int object,other,hit,increase;
    world_object *o;
    unsigned i;
    if(v->state) return 0;
    object=world_fxo_object(w,(unsigned)p[3]);other=world_fxo_object(w,(unsigned)p[11]);
    if(object<0 || other<0) return -1;
    o=&w->objects[object];motion_start(v,w,(unsigned)object);
    increase=p[9]==p[1];step=(int32_t)((int64_t)p[4]*s->frame_ticks/2);
    o->y+=(increase?step:-step);
    if(increase && o->y>p[5]) o->y=p[5];
    if(!increase && o->y<p[6]) o->y=p[6];
    hit=events_ycrush_crash(s,a,w,(unsigned)object,(unsigned)other);
    if(hit<0) return -1;
    if(hit) {
        o->y+=increase?-2*p[4]:2*p[4];
        if(increase && o->y<p[6]) o->y=p[6];
        if(!increase && o->y>p[5]) o->y=p[5];
    }
    if((increase && o->y>=p[5]) || (!increase && o->y<=p[6])) motion_stop(v,w,(unsigned)object);
    for(i=0;i<4;++i) o->corner_y[i]=o->y;
    for(i=0;i<2;++i) {
        other=world_fxo_object(w,(unsigned)p[i?8:0]);
        if(other<0) return -1;
        if(other) {
            world_object *linked=&w->objects[other];
            linked->y=o->y+p[2];
            linked->corner_y[0]=linked->corner_y[3]=linked->y-linked->radius;
            linked->corner_y[1]=linked->corner_y[2]=linked->y+linked->radius;
        }
    }
    return 0;
}

static int move_fxo(events_state *e, sim_state *s, ai_state *a, world *w, events_event *v)
{
    int object = world_fxo_object(w,(unsigned)v->value[3]);
    world_object *p;
    int32_t *axis, old, step;
    int increase, reached = 0;
    unsigned i;
    if (object < 0) return cm_fail("MoveWorld/motion",(uint32_t)v->value[3],"invalid moving FXO");
    p = &w->objects[object];
    axis = v->type == 6 ? &p->z : v->type == 7 ? &p->x : &p->y;
    if (!v->state) {
        motion_start(v,w,(unsigned)object);
        old = *axis; step = (int32_t)((int64_t)v->value[4] * s->frame_ticks / 2);
        increase = v->value[1] == v->value[9];
        if (v->type != 6) world_fxo_map_delete(w,(unsigned)object);
        *axis = (int32_t)((uint32_t)*axis + (increase ? (uint32_t)step : 0u - (uint32_t)step));
        if (increase && *axis >= v->value[5]) { *axis = v->value[5]; reached = 1; }
        if (!increase && *axis <= v->value[6]) { *axis = v->value[6]; reached = 1; }
        if (v->type == 6) {
            int hit;
            int32_t z=p->z;
            p->z=old;
            hit=move_planes(e,s,a,w,v,(unsigned)object,z,increase,1);
            if(hit<0) return -1;
            if(hit) {
                z=(int32_t)((uint32_t)p->z+(increase?0u-(uint32_t)v->value[4]*2u:(uint32_t)v->value[4]*2u));
                if(increase && z<v->value[6]) z=v->value[6];
                if(!increase && z>v->value[5]) z=v->value[5];
                if(move_planes(e,s,a,w,v,(unsigned)object,z,!increase,0)<0) return -1;
                reached=increase?p->z>=v->value[5]:p->z<=v->value[6];
            }
        }
        if (v->type != 6) {
            world_fxo_map_insert(w,(unsigned)object);
            for (i = 0; i < 4; ++i) {
                if (v->type == 7) p->corner_x[i] = p->x + ((i == 0 || i == 3) ? -p->radius : p->radius);
                else p->corner_y[i] = p->y + (i == 0 || i == 3 ? -p->radius : p->radius);
            }
        }
        if (reached) {
            motion_stop(v,w,(unsigned)object);
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
    case 2: case 21:
        if (v->type == 21) {
            if((unsigned)p[3]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid gated tile switch");
            if(e->switches[p[3]].state!=p[9]) break;
        }
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
    case 4: case 22: case 37:
        if (v->type == 37) {
            if ((unsigned)p[8]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid gate switch");
            if (e->switches[p[8]].state!=p[9]) break;
        }
        if ((unsigned)p[3] >= e->switch_count || !target) return cm_fail("MoveWorld",index,"invalid switch target");
        n = e->switches[p[3]].state;
        if ((v->type != 22 ? n != target->value[1] : n == target->value[1])) {
            if (event_set(e,p[0],-1)) return -1;
        } else if (v->type==37 && p[11]==1 && n==p[15] && target->state) {
            if (timer_toggle(e,s,p[0])) return -1;
            if (!target->state && events_toggle_switch(e,w,(unsigned)p[3])) return -1;
        }
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
            if (events_toggle_switch(e,w,(unsigned)p[3])) return -1;
        } else {
            int running=target && target->state;
            if (timer_toggle(e,s,p[0])) return -1;
            if (v->type==11 && running && !target->state &&
                events_toggle_switch(e,w,(unsigned)p[3])) return -1;
        }
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
            e->text_serial=snd_text()->revision;
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
    case 25:
        if((p[2] && tile!=p[2]) || s->pc_mode==7 || v->state) break;
        if(p[0]<0 || p[0]>=12) return cm_fail("MoveWorld/scene",index,"invalid destination scene");
        e->scene_requested.pending=1;e->scene_requested.scene=p[0];
        e->scene_requested.x=p[6];e->scene_requested.y=p[5];e->scene_requested.z=p[4];
        e->scene_requested.heading=(uint16_t)p[10];e->scene_requested.auto_mode=p[3];
        if(p[8] && events_toggle_switch(e,w,(unsigned)p[8])) return -1;
        v->state=1;once(v);
        break;
    case 26:
        if(v->state) break;
        e->shop_requested.kind=p[0]; e->shop_requested.switch_index=p[3];
        e->shop_requested.screen=p[9]; e->shop_requested.arg=p[8];
        v->state=1; p[1]=0;
        break;
    case 27:
        if(v->state) break;
        if(events_toggle_switch(e,w,(unsigned)p[3])) return -1;
        v->state=1; p[1]=0; once(v);
        break;
    case 28:
        w->fog_on=(uint8_t)p[3];
        if(p[8]) w->sky_type=p[11];
        break;
    case 29:
        if(v->state) break;
        if(p[3]<0 || p[8]<p[3] || (unsigned)p[8]>=w->person_count)
            return cm_fail("MoveWorld",index,"invalid actor interval");
        for(i=(unsigned)p[3];i<=(unsigned)p[8];++i)
            if(w->persons[i].state==9) { w->persons[i].state=1; w->persons[i].unk_001E=0; }
        v->state=1; once(v);
        break;
    case 30:
        if(v->state) break;
        object=actor_object(w,p[3]);
        if(object<0) return cm_fail("MoveWorld",index,"invalid reset actor");
        world_omap_delete(w,(unsigned)object);
        if(ai_set_person(a,w,(unsigned)p[3],(unsigned)p[8],(int16_t)p[9],
            p[6],p[5],p[4],p[2],(uint16_t)((uint32_t)p[9]>>16))) return -1;
        world_omap_insert(w,(unsigned)w->persons[p[3]].object);
        v->state=1;once(v);break;
    case 31:
        if(v->state) break;
        if(p[3]<0 || (unsigned)p[3]>=w->person_count) return cm_fail("MoveWorld",index,"invalid dead actor");
        if(w->persons[p[3]].status!=2) break;
        if((p[0] && event_set(e,p[0],-1)) || (p[8] && event_set(e,p[8],-1))) return -1;
        if((p[9] && set_enabled(e,p[9],0)) || (p[7] && set_enabled(e,p[7],0)) ||
           (p[15] && set_enabled(e,p[15],0))) return -1;
        v->state=1; once(v); break;
    case 32:
        if(v->state) break;
        if(!target) return cm_fail("MoveWorld",index,"invalid event parameter target");
        target->value[3]=p[3]; v->state=1; once(v); break;
    case 33:
        if(v->state || !target || !target->state) break;
        if(target->value[1]!=p[9]) return event_set(e,p[0],-1);
        if(timer_toggle(e,s,p[0])) return -1;
        if(!target->state) { v->state=1; p[1]^=1; }
        break;
    case 34:
        if(v->state) break;
        n=-1;
        for(i=0;i<w->person_count;++i) if(w->persons[i].unk_0002==p[0]) { n=w->persons[i].type; break; }
        if(n<0) return cm_fail("MoveWorld",index,"actor template not loaded");
        if(p[3]<0 || p[8]<p[3] || (unsigned)p[8]>=w->person_count) return cm_fail("MoveWorld",index,"invalid actor range");
        for(i=(unsigned)p[3];i<=(unsigned)p[8];++i) {
            w->persons[i].unk_0002=(int16_t)p[0]; w->persons[i].type=(int16_t)n; w->persons[i].target=-1;
            if(p[9]) { w->persons[i].unk_0090=p[9]; w->persons[i].unk_0094=p[7]; }
        }
        v->state=1; once(v); break;
    case 35:
        if(p[2] && tile!=p[2]) break;
        for(i=0;i<EVENTS_ITEM_MAX;++i)
            if(e->items[i].type>0 && e->items[i].kind==14 && e->items[i].subtype==p[3]) {
                if(event_set(e,p[0],-1)) return -1;
                if(p[15]) { e->item_up_type=p[15]; e->item_up_time=e->ticks+120; }
                once(v);
            }
        break;
    case 36:
        if(v->state) break;
        if((unsigned)p[3]>=32) return cm_fail("MoveWorld/light",index,"invalid light index");
        if(p[11]) w->light_param[p[3]]=(int16_t)p[2];
        n=(int16_t)(uint16_t)(w->light_param[p[3]]+(p[1]?p[4]:-p[4]));
        if(p[1] && n>p[5]) { n=p[5];p[1]=0; }
        if(!p[1] && n<p[6]) { n=p[6];p[1]=1; }
        w->light_param[p[3]]=(int16_t)n;
        if(p[11]) {
            p[2]=n;
            if(n<64) w->light_type[p[3]]=0;
            else if(n<68) w->light_type[p[3]]=(uint16_t)p[11];
            else if(n<78) { w->light_type[p[3]]=1;w->light_param[p[3]]=(int16_t)(n-68); }
            else { w->light_type[p[3]]=0;w->light_param[p[3]]=63; }
        }
        once(v); break;
    case 38: {
        events_event *relay=event_at(e,p[3]);
        if(!target || !relay) return cm_fail("MoveWorld",index,"invalid threshold relay");
        n=target->state>p[7] && target->value[1]==p[9];
        if(n?relay->value[1]==p[11]:relay->value[1]!=p[11]) {
            if(event_set(e,p[3],-1) || (p[8] && event_set(e,p[8],-1))) return -1;
        }
        break;
    }
    case 39:
        if(!target || (target->state && target->value[1]!=p[9])) break;
        if(p[3]<0 || (unsigned)p[3]>=w->person_count || p[8]<0 || (unsigned)p[8]>=w->person_count)
            return cm_fail("MoveWorld",index,"invalid cooldown actor");
        w->persons[p[3]].unk_001E=(int16_t)p[7];
        if(p[8]) w->persons[p[8]].unk_001E=(int16_t)p[7];
        break;
    case 40:
        if((unsigned)p[8]>=e->switch_count || (unsigned)p[11]>=e->switch_count ||
           (unsigned)p[7]>=e->switch_count || (unsigned)p[3]>=e->switch_count)
            return cm_fail("MoveWorld",index,"invalid triple switch gate");
        if(e->switches[p[8]].state==1 && e->switches[p[11]].state==1 &&
           e->switches[p[7]].state==0) {
            if(!target) return cm_fail("MoveWorld",index,"invalid triple switch target");
            if(e->switches[p[3]].state!=target->value[1]) return event_set(e,p[0],-1);
        } else if(e->switches[p[3]].state==1) return event_set(e,p[9],-1);
        break;
    case 41:
        if((unsigned)p[3]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid enable switch");
        if(e->switches[p[3]].state!=p[9]) break;
        if((p[0] && set_enabled(e,p[0],p[11])) || (p[13] && set_enabled(e,p[13],p[14]))) return -1;
        if(p[8]) {
            events_event *relay=event_at(e,p[8]);
            if(!relay) return cm_fail("MoveWorld",index,"invalid parameter relay");
            relay->value[1]=p[7];
        }
        once(v);break;
    case 42:
        if(v->state) break;
        e->autoDestruct=4;e->autoDTime=e->ticks+0x41A0u;
        e->autoMSound=0;e->autoMSlot=-1;e->autoDMsgTimer=e->ticks+160;
        e->autoLFlag=1;v->state=1;once(v);break;
    case 43:
        w->shade_override.bias=p[0];w->shade_override.shift=p[3];w->shade_override.scale=p[8];
        ++w->shade_override.revision;
        break;
    case 44: return rotate_fxo(w,s,v);
    case 45: return ycrush_fxo(s,a,w,v);
    case 46:
        for(i=0;i<w->person_count;++i) {
            object=actor_object(w,(int32_t)i);
            if(object<0) return cm_fail("MoveWorld",index,"invalid teleport actor");
            if(tile_index(&w->objects[object],INT32_MIN)!=p[2] || !w->objects[object].active) continue;
            if(move_actor(w,object,p[6],p[5],p[4],w->objects[object].heading)) return -1;
            if((unsigned)object==w->player_object) e->unk_1444EC=3;
            if(p[13]) (void)snd_object(w,(unsigned)p[13],(unsigned)object,0x60000,32767,0);
        }
        break;
    case 47:
        if((unsigned)p[3]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid enable switch");
        return set_enabled(e,p[0],e->switches[p[3]].state==p[9]);
    case 48:
        if((unsigned)p[11]>=96 || (!p[15] && (unsigned)p[3]>=e->switch_count))
            return cm_fail("MoveWorld",index,"invalid aperture controller");
        w->apertures[p[11]].enabled=(uint8_t)(p[15]?p[1]:e->switches[p[3]].state);
        break;
    case 49:
        if(event_tile(w,p[3],tile)!=p[2]) break;
        if((unsigned)p[8]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid position switch");
        if(e->switches[p[8]].state==p[9]) break;
        if(events_toggle_switch(e,w,(unsigned)p[8])) return -1;
        once(v);break;
    case 50:
        if(event_tile(w,p[3],tile)!=p[2]) break;
        if(v->state) { if(s->bp_limit_flag) --v->state;break; }
        if(w->unk_144AA4>=w->object_count) return cm_fail("MoveWorld",index,"invalid missile origin");
        {
            world_object *origin=&w->objects[w->unk_144AA4];
            origin->x=p[6];origin->y=p[5];origin->floor_z=p[4];
            origin->heading=(uint16_t)p[10];origin->z=origin->height=0;
            if(ai_launch_missile(a,s,w,w->unk_144AA4,w->player_object,37)) return -1;
        }
        v->state=(uint8_t)p[7];break;
    case 51:
        if(v->state) break;
        for(i=0;i<EVENTS_ITEM_MAX;++i)
            if(e->items[i].type>0 && e->items[i].kind==14 && e->items[i].subtype==p[3]) {
                e->items[i].type=0;v->state=1;once(v);
            }
        break;
    case 52:
        if(v->state) break;
        if(p[7]>p[0]) { ++p[0];break; }
        p[0]=0;
        if(p[3]<0 || (unsigned)p[3]>=w->person_count) return cm_fail("MoveWorld",index,"invalid relocated actor");
        if(p[2] && e->text_trigger && e->text_object==actor_object(w,p[2])) {
            e->text_trigger=0;snd_message_stop();
        }
        {
            world_person *person=&w->persons[p[3]];
            static const int16_t states[5]={-1,9,10,3,0};
            if(p[11]>=0 && p[11]<5 && states[p[11]]>=0) person->state=states[p[11]];
            if(p[9]) {
                uint32_t max=(uint32_t)person->unk_0038>>16;
                person->unk_0038=(int32_t)(max<<16|max); person->status=0;
            }
            if(p[13]) (void)snd_object(w,(unsigned)p[13],(unsigned)person->object,0x60000,40959,0);
            object=person->object;
            if(object<0 || (unsigned)object>=w->object_count) return cm_fail("MoveWorld",index,"invalid relocation object");
            if(w->objects[object].active) {
                const world_object *pc=&w->objects[w->player_object];
                int32_t x=p[6],y=p[5],z=p[4]; uint16_t angle=(uint16_t)p[10];
                if(p[15]) {
                    unsigned trig=(uint16_t)(pc->heading+0x8000u)>>5;
                    x=pc->x+(int32_t)((int64_t)0x7000*w->trig.cos[trig]>>16);
                    y=pc->y+(int32_t)((int64_t)0x7000*w->trig.sin[trig]>>16);
                    z=pc->floor_z;angle=pc->heading;
                }
                if(move_actor(w,object,x,y,z,angle)) return -1;
            }
        }
        if((p[8] && set_enabled(e,p[8],0)) || (p[14] && set_enabled(e,p[14],1))) return -1;
        v->state=1; once(v);break;
    case 53:
        if(v->state) break;
        if(p[3]<0 || (unsigned)p[3]>=w->person_count) return cm_fail("MoveWorld",index,"invalid wounded actor");
        if((int16_t)w->persons[p[3]].unk_0038 >= (a->difficulty?p[8]:p[8]/2)) break;
        if(p[9]) {
            uint32_t max=(uint32_t)w->persons[p[3]].unk_0038>>16;
            w->persons[p[3]].unk_0038=(int32_t)(max<<16|max);w->persons[p[3]].status=0;
        }
        if((p[0] && set_enabled(e,p[0],0)) || (p[7] && set_enabled(e,p[7],0)) ||
           (p[13] && set_enabled(e,p[13],0)) || (p[11] && event_set(e,p[11],-1))) return -1;
        v->state=1;once(v);break;
    case 54:
        object=actor_object(w,p[3]);
        if(object<0 || !target) return cm_fail("MoveWorld",index,"invalid paired presence actor/event");
        n=tile_index(&w->objects[object],INT32_MIN);
        n=n==p[2] || n==p[6] || tile==p[2] || tile==p[6];
        if(target->value[1]==!n) return event_set(e,p[0],n);
        break;
    case 55:
        if(v->state) break;
        v->state=1;
        return event_set(e,p[0],p[1]);
    case 56:
        if(v->state) break;
        if((unsigned)p[3]>=3) return cm_fail("MoveWorld",index,"invalid coder row");
        for(i=0;i<4;++i) {
            n=world_rnd(&w->rng,i==0 || i==3?4:8)+(i==3?4:0);
            e->coderStr[p[3]*8+i]=(char)(n<4?'A'+n:'-'+n);
        }
        e->coderStr[p[3]*8+4]=0;snd_code(e->coderStr);
        v->state=1;once(v);break;
    case 57:
        if(v->state) break;
        if(p[3]<0 || p[8]<p[3] || (unsigned)p[8]>w->person_count)
            return cm_fail("MoveWorld",index,"invalid surviving actor range");
        n=0;
        for(i=(unsigned)p[3];i<(unsigned)p[8];++i) n+=w->persons[i].status!=2;
        if(n>p[13]) break;
        if((p[0] && (set_enabled(e,p[0],1) || event_set(e,p[0],-1))) ||
           (p[11] && (set_enabled(e,p[11],1) || event_set(e,p[11],-1))) ||
           (p[9] && set_enabled(e,p[9],0)) || (p[7] && set_enabled(e,p[7],0))) return -1;
        if(p[2] && e->text_trigger && e->text_object==actor_object(w,p[2])) {
            e->text_trigger=0;snd_message_stop();
        }
        v->state=1;once(v);break;
    case 58:
        if(v->state || !target || !target->state) break;
        if(event_set(e,p[3],-1)) return -1;
        v->state=1;once(v);break;
    case 59:
        if(v->state) break;
        if(p[3]<0 || p[8]<p[3] || (unsigned)p[8]>=w->person_count) return cm_fail("MoveWorld",index,"invalid spawning actor range");
        {
            int32_t x=0x4000,y=0x4000;
            int32_t width=(int32_t)((uint32_t)p[9]<<16);
            if(width<0x10000) width=0x10000;
            for(i=(unsigned)p[3];i<=(unsigned)p[8];++i) {
                world_person *person=&w->persons[i];
                if(person->object<0 || (unsigned)person->object>=w->object_count) return cm_fail("MoveWorld",index,"invalid spawning actor object");
                if(w->objects[person->object].active) {
                    world_omap_delete(w,(unsigned)person->object);
                    w->objects[person->object].active=0;
                }
                if(ai_set_person(a,w,i,(unsigned)person->type,p[0],p[6]+x,p[5]+y,0,p[4],(uint16_t)((uint32_t)p[9]>>16))) return -1;
                world_omap_insert(w,(unsigned)w->persons[i].object);
                x+=0x4000;
                if(x>=width) { x=0x4000;y+=0x4000; }
            }
        }
        v->state=1;once(v);break;
    case 60: return monitor(e,w,v);
    case 61:
        if(v->state) break;
        {
            static const unsigned fields[7]={0,3,8,11,7,9,13};
            events_event *video=event_at(e,p[15]);
            if(!video) return cm_fail("MoveWorld/video",index,"invalid monitor target");
            for(i=0;i<7;++i) e->video_list[i]=p[fields[i]];
            e->video_list[7]=0;video->value[9]=0;
        }
        v->state=1;once(v);break;
    case 62: {
        const world_object *pc=&w->objects[w->player_object];
        if(e->ticks>(uint32_t)p[12] && pc->x>p[6] && pc->x<p[4] &&
           pc->y>p[5] && pc->y<p[10] && !e->text_trigger) {
            object=world_fxo_object(w,(unsigned)p[3]);
            if(object<0) return -1;
            p[12]=(int32_t)(e->ticks+(uint32_t)p[8]*40u);
            if(start_text(e,w,w->scene,p[0],object,2)) return -1;
        }
        return talk_texture(e,s,w,v,0);
    }
    case 63:
        n=event_tile(w,p[3],tile);
        if(n!=p[2] && n!=p[6]) break;
        if(!target) return cm_fail("MoveWorld",index,"invalid linked tile target");
        if(target->value[1]!=p[1]) {
            static const unsigned fields[5]={0,8,11,7,9};
            for(i=0;i<5;++i) if(p[fields[i]] && event_set(e,p[fields[i]],p[1])) return -1;
        }
        once(v);break;
    case 64:
        if(!target) return cm_fail("MoveWorld",index,"invalid presence target");
        n=tile==p[2] || tile==p[6];
        for(i=0;!n && i<w->person_count;++i)
            if(w->persons[i].status!=2 && (event_tile(w,(int32_t)i,tile)==p[2] ||
                                         event_tile(w,(int32_t)i,tile)==p[6])) n=1;
        if(target->value[1]==!n) return event_set(e,p[0],n);
        break;
    case 65:
        p[0]=(p[0]+1)%4;
        for(i=3;i<15;++i) if(p[i]>0) {
            object=world_fxo_object(w,(unsigned)p[i]);
            if(object<0) return -1;
            if(!(p[0]&1)) w->objects[object].heading=(uint16_t)(w->objects[object].heading-0x8000u);
            if(set_texture(w,p[i],p[0]&1?p[15]:p[i])) return -1;
        }
        break;
    case 66:
        if(!target || target->state) break;
        if(p[1]!=target->value[1]) {
            p[1]=target->value[1];
            for(i=3;i<15;++i) if(p[i]>0 && event_set(e,p[i],-1)) return -1;
        }
        once(v);break;
    case 67:
        if(v->state) break;
        if(e->cash>=p[2]) {
            events_cash_add(e,-p[2]);
            if(event_set(e,p[3],1)) return -1;
            if(p[7]>0) {
                if((unsigned)p[7]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid paid switch");
                if(e->switches[p[7]].state!=p[9] && events_toggle_switch(e,w,(unsigned)p[7])) return -1;
            }
            if(p[13] && event_set(e,p[13],1)) return -1;
            if((p[4] && set_enabled(e,p[4],0)) || (p[10] && set_enabled(e,p[10],0)) ||
               (p[14] && set_enabled(e,p[14],0)) || (p[15] && set_enabled(e,p[15],0))) return -1;
            once(v);
        } else {
            if(event_set(e,p[8],1)) return -1;
            if(p[11]) once(v);
        }
        v->state=1;break;
    case 68: case 69:
        if(v->state || tile!=p[2]) break;
        if(e->event_count<243 || w->person_count<61)
            return cm_fail("MoveWorld/arena",index,"missing arena actors/events");
        if(v->type==68) {
            e->events[24].enabled=e->events[25].enabled=e->events[66].enabled=1;
            e->events[24].value[3]=e->events[25].value[3]=e->events[66].value[3]=1;
            e->events[28].value[3]=0;e->events[243].value[1]=0;
            e->PCBetPick=-1;e->PCBet=0;
            for(i=57;i<61;++i) {
                object=actor_object(w,(int32_t)i);
                if(object<0) return cm_fail("MoveWorld/arena",index,"invalid arena actor");
                if(move_actor(w,object,0x1E8000,0x1F8000,w->objects[object].floor_z,w->objects[object].heading)) return -1;
                w->persons[i].state=9;
            }
        } else if(w->persons[57].state==9 || w->persons[57].status==2 ||
                  (w->persons[58].status==2 && w->persons[59].status==2 && w->persons[60].status==2)) {
            static const unsigned types[4]={20,5,4,5};
            static const int32_t xs[4]={0x1B2000,0x1BC000,0x1B8000,0x1B4000};
            static const int32_t ys[4]={0xE8000,0xAC000,0xA4000,0xAC000};
            for(i=0;i<4;++i) {
                object=actor_object(w,(int32_t)i+57);
                if(object<0) return cm_fail("MoveWorld/arena",index,"invalid arena actor");
                world_omap_delete(w,(unsigned)object);
                if(ai_set_person(a,w,i+57,types[i],99,xs[i],ys[i],0,0,0)) return -1;
                world_omap_insert(w,(unsigned)w->persons[i+57].object);
            }
            e->events[24].enabled=e->events[25].enabled=e->events[66].enabled=1;
            e->events[24].value[3]=e->events[25].value[3]=e->events[66].value[3]=57;
            e->events[28].value[3]=59;e->events[243].value[1]=0;
            if(!e->text_trigger && p[0]) {
                object=world_fxo_object(w,(unsigned)p[3]);
                if(object<0 || start_text(e,w,w->scene,p[0],object,2)) return -1;
            }
        }
        v->state=1;p[1]^=1;break;
    case 70:
        if(!target) return cm_fail("MoveWorld",index,"invalid key presence target");
        if(tile!=p[2] && tile!=p[6]) {
            p[1]=0;
            if(target->value[1]==1) return event_set(e,p[0],0);
            break;
        }
        n=0;
        for(i=0;i<EVENTS_ITEM_MAX;++i)
            if(e->items[i].type>0 && e->items[i].kind==14 && e->items[i].subtype==p[3]) n=1;
        if(n && !target->value[1]) {
            if(p[9]!=2 && !e->text_trigger) {
                if(p[8] && start_text(e,w,w->scene,p[8],0,2)) return -1;
                p[9]=2;
            }
            return event_set(e,p[0],1);
        }
        if(!n && !p[1]) {
            if(!p[9] && !e->text_trigger) {
                if(p[11] && start_text(e,w,w->scene,p[11],0,2)) return -1;
                p[9]=1;
            }
            p[1]=1;
        }
        break;
    case 71:
        if((unsigned)p[3]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid random switch");
        if(e->switches[p[3]].state!=p[9]) break;
        if(event_set(e,world_rnd(&w->rng,2)?p[0]:p[8],-1)) return -1;
        once(v);break;
    case 72:
        if((unsigned)p[3]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid underwater switch");
        a->pc.underwater=e->switches[p[3]].state==p[9]?p[8]:p[11];break;
    case 73:
        if(v->state) break;
        if(p[3]<0 || (unsigned)p[3]>=w->person_count) return cm_fail("MoveWorld",index,"invalid actor field");
        w->persons[p[3]].unk_00B8=(int16_t)p[8];
        w->persons[p[3]].unk_00BA=(int16_t)((uint32_t)p[8]>>16);
        v->state=1;once(v);break;
    case 74:
        if(!v->state && !e->text_trigger) {
            if(p[8]>=3) { e->ending_requested=1;v->state=1;once(v); }
            else {
                object=world_fxo_object(w,(unsigned)p[3]);
                if(object<0) return -1;
                if(start_text(e,w,w->scene,p[0]+p[8],object,2)) return -1;
                ++p[8];
            }
        }
        return talk_texture(e,s,w,v,1);
    case 75:
        if(v->state || tile!=p[6]) break;
        if(target) target->state=0;
        snd_song_end();
        if(p[3]?snd_song(p[3],p[7]):snd_song_default(p[7])) return -1;
        v->state=1;break;
    case 76: case 77:
        if(v->state) break;
        if(v->type==77 && p[8]) {
            if(p[8]<0 || (unsigned)p[8]>=w->person_count) return cm_fail("MoveWorld",index,"invalid follower actor");
            if(w->persons[p[8]].status==2) { v->state=1;once(v);break; }
            w->persons[p[8]].unk_00A0=0;
        }
        if(p[3]<0 || (unsigned)p[3]>=w->prop_count) return cm_fail("MoveWorld",index,"invalid activated prop");
        object=w->prop_objects[p[3]];
        if(object) {
            const world_object *pc=&w->objects[w->player_object];
            if(w->objects[object].active) world_omap_delete(w,(unsigned)object);
            w->objects[object].active=1;
            if(move_actor(w,object,v->type==76?p[6]:pc->x,v->type==76?p[5]:pc->y,
                v->type==76?w->objects[object].floor_z:pc->floor_z,w->objects[object].heading)) return -1;
            p[1]=1;
        }
        v->state=1;once(v);break;
    case 78:
        if((unsigned)p[3]>=e->switch_count || !target) return cm_fail("MoveWorld",index,"invalid one-shot switch relay");
        if(e->switches[p[3]].state!=target->value[1]) {
            if(event_set(e,p[0],-1)) return -1;
            once(v);
        }
        break;
    case 79:
        if(v->state) break;
        if(e->ambient_slot!=-1) { snd_abort(e->ambient_slot,(unsigned)e->ambient_sample);e->ambient_slot=-1; }
        if(p[1]==1) {
            if(p[8]) {
                e->ambient_slot=snd_loop((unsigned)p[3],p[6],(unsigned)p[5]);
                if(e->ambient_slot!=-1) v->state=1;
                e->ambient_sample=p[3];e->ambient_until=e->ticks+(uint32_t)p[7]*40u;
            } else { (void)snd_play((unsigned)p[3],p[6],(unsigned)p[5]);v->state=1; }
        } else v->state=1;
        break;
    case 80:
        if(v->state) break;
        object=actor_object(w,p[3]);
        if(object<0) return cm_fail("MoveWorld",index,"invalid travelling actor");
        {
            world_person *person=&w->persons[p[3]];
            world_object *o=&w->objects[object];
            if((o->x>>16)==(p[6]>>16) && (o->y>>16)==(p[5]>>16)) {
                person->state=9;
                if(move_actor(w,object,0x1F8000,0x1F8000,o->floor_z,o->heading)) return -1;
                v->state=1;once(v);
            } else {
                uint32_t max=(uint32_t)person->unk_0038>>16;
                o->target_mode=1;o->target_angle=cm_atan64k(o->x,o->y,p[6],p[5]);
                person->state=0;person->unk_001E=1000;person->status=0;
                person->unk_0038=(int32_t)(max<<16|max);
            }
            if(p[4]>person->unk_0060) {
                person->unk_0060+=0x800*s->frame_ticks/2;
                if(person->unk_0060>p[4]) person->unk_0060=p[4];
            }
        }
        break;
    case 81:
        if((unsigned)p[3]>=e->switch_count || !target) return cm_fail("MoveWorld",index,"invalid locking switch relay");
        if(e->switches[p[3]].state==target->value[1]) break;
        if(event_set(e,p[0],-1)) return -1;
        e->switches[p[3]].key=999;v->enabled=0;break;
    case 82:
        if(v->state) break;
        s->fire_check=p[3];s->fire_damage=p[8];s->fire_tile_panel=p[11];s->fire_tile_art=p[7];
        if(p[13]) (void)snd_play((unsigned)p[13],32767,32768);
        v->state=1;once(v);break;
    case 83:
        if(p[3]<0 || (unsigned)p[3]>=w->prop_count ||
           (unsigned)p[7]>=e->switch_count || (unsigned)p[11]>=e->switch_count)
            return cm_fail("MoveWorld",index,"invalid alternating prop");
        object=w->prop_objects[p[3]];
        if((unsigned)object>=w->object_count) return cm_fail("MoveWorld",index,"invalid alternating prop object");
        {
            world_object *o=&w->objects[object],*pc=&w->objects[w->player_object];
            if(!o->active || ((pc->floor_z+0x4800)>>16)!=(o->floor_z>>16) ||
               cm_distance(o->x,o->y,pc->x,pc->y)>=p[2]) break;
            p[9]^=1;
            if(move_actor(w,object,p[p[9]?6:4],p[p[9]?5:10],o->floor_z,o->heading)) return -1;
            e->switches[p[p[9]?11:7]].key=(int16_t)p[8];
            e->switches[p[p[9]?7:11]].key=999;
            if(p[13]) (void)snd_play((unsigned)p[13],32767,32768);
        }
        break;
    case 84:
        if((unsigned)p[0]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid item exchange switch");
        if(e->switches[p[0]].state!=p[9]) break;
        for(i=0;i<EVENTS_ITEM_MAX;++i) if(e->items[i].type>0 && e->items[i].kind==14 && e->items[i].subtype==p[3]) {
            e->items[i].type=0;
            if(p[8]<0 || (unsigned)p[8]>=w->prop_count) return cm_fail("MoveWorld",index,"invalid exchanged prop");
            object=w->prop_objects[p[8]];
            if(object) { w->objects[object].active=1;if(move_actor(w,object,p[6],p[5],p[4],w->objects[object].heading)) return -1; }
            once(v);
        }
        break;
    case 85:
        if((unsigned)p[3]>=e->switch_count || (unsigned)p[8]>=e->switch_count || !target)
            return cm_fail("MoveWorld",index,"invalid paired switch relay");
        n=e->switches[p[3]].state==1 && e->switches[p[8]].state==1;
        if(n?(p[1]!=target->value[1]):(target->value[1]==p[1]))
            return event_set(e,p[0],-1);
        break;
    case 86:
        for(i=3;i<15;++i) if(p[i]>0) {
            if((unsigned)p[i]>=w->person_count) return cm_fail("MoveWorld",index,"invalid actor trigger");
            if((w->persons[p[i]].state==8 || w->persons[p[i]].state==6) &&
                (p[15]==1 || w->persons[p[i]].status!=2)) {
                if(p[0] && event_set(e,p[0],-1)) return -1;
                once(v);
            }
        }
        break;
    case 87:
        if(!s->bp_limit_flag) break;
        if(p[3]<0 || (unsigned)p[3]>=w->prop_count)
            return cm_fail("MoveWorld",index,"invalid draining prop");
        object=w->prop_objects[p[3]];
        if((unsigned)object>=w->object_count) return cm_fail("MoveWorld",index,"invalid draining prop object");
        {
            world_object *o=&w->objects[object],*pc=&w->objects[w->player_object];
            int distance=cm_distance(o->x,o->y,pc->x,pc->y),near=distance<=0x5000;
            o->flags&=~0x2000u;
            if(!near) {
                if(e->ticks<=e->DrakMangTime) break;
                e->DrakMangTime=e->ticks+(uint32_t)(distance/0x5000);
                n=distance<0x30000?world_rnd(&w->rng,2):0;
            } else n=world_rnd(&w->rng,2);
            if(!n) break;
            e->unk_144B58=near;e->unk_144B5C=e->ticks+40;
            if(near) {
                o->flags|=0x2000u;
                (void)snd_play(world_rnd(&w->rng,3)?28:29,32767,32768);
            }
            if(ai_damage_person(a,w,w->player_person,n,0,0)) return -1;
            e->energy=(int16_t)(uint16_t)(e->energy-n);
            if(e->energy<0) e->energy=0;
            if(!near) { if(!e->unk_1444E8) e->unk_1444E8=2;break; }
            if(!world_rnd(&w->rng,4)) {
                if(w->person_count<2) return cm_fail("MoveWorld",index,"missing affected actor");
                if(w->persons[1].status!=2 && (w->persons[1].state==0 || w->persons[1].state==1)) {
                    w->persons[1].state=6;w->persons[1].unk_001E=2;
                }
            }
            e->unk_1444E8=0;e->unk_1444EC=world_rnd(&w->rng,5)+1;
            p[7]-=n;
            if(p[7]<=0) {
                if(p[8]<0 || (unsigned)p[8]>=w->person_count)
                    return cm_fail("MoveWorld",index,"invalid restored actor");
                w->persons[p[8]].unk_00B8=8;w->persons[p[8]].unk_00BA=0;
                {
                    uint32_t max=(uint32_t)w->persons[p[8]].unk_0038>>16;
                    w->persons[p[8]].unk_0038=(int32_t)(max<<16|max);w->persons[p[8]].status=0;
                }
                p[7]=0;
                if(set_enabled(e,p[0],1) || event_set(e,p[0],-1)) return -1;
                world_omap_delete(w,(unsigned)object);o->active=0;o->x=o->y=0x1F8000;
                (void)snd_play(29,32767,32768);once(v);
            } else if(events_rescale_prop(w,(unsigned)p[3],10+(p[7]>100?100:p[7])*9/10)) return -1;
        }
        break;
    case 88: {
        const world_object *pc=&w->objects[w->player_object];
        if(!target || p[1]==target->value[1]) break;
        if(pc->x>=p[6] && (int64_t)pc->x<(int64_t)p[6]+(int64_t)p[11]*65536 &&
           pc->y>=p[5] && (int64_t)pc->y<(int64_t)p[5]+(int64_t)p[7]*65536 &&
           pc->floor_z>=p[4] && (int64_t)pc->floor_z<(int64_t)p[4]+(int64_t)p[9]*65536) {
            if(event_set(e,p[0],-1)) return -1;
            once(v);
        }
        break;
    }
    case 89:
        if(p[6]>=w->objects[w->player_object].floor_z) break;
        if(p[3]<0 || p[8]<p[3] || (unsigned)p[8]>=w->person_count) return cm_fail("MoveWorld",index,"invalid waking actor range");
        for(i=(unsigned)p[3];i<=(unsigned)p[8];++i) if(w->persons[i].status!=2) { w->persons[i].state=0;w->persons[i].unk_001E=10; }
        break;
    case 90:
        if((unsigned)p[3]>=e->switch_count) return cm_fail("MoveWorld",index,"invalid edge selector");
        n=e->switches[p[3]].state;
        if(n==p[1]) break;
        p[1]=n;
        return event_set(e,n?p[0]:p[8],1);
    default: return cm_fail("MoveWorld/type",v->type,"invalid event type");
    }
    return 0;
}

int events_step(events_state *e, sim_state *s, ai_state *a, world *w)
{
    unsigned i, event, hops;
    int tile;
    if (!e || !s || !a || !w || w->player_object >= w->object_count) return cm_fail("MoveWorld",0,"missing simulation state");
    e->ticks = s->timer_ticks;
    e->actors = a;
    {
        int chain=snd_take_video();
        if(chain) e->video_chain=chain;
    }
    if(e->ambient_slot!=-1 && e->ticks>=e->ambient_until) {
        snd_abort(e->ambient_slot,(unsigned)e->ambient_sample);e->ambient_slot=-1;
    }
    if(dialogue_tick(e,w)) return -1;
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
    if(events_cosmetics(e,s,a,w)) return -1;
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
