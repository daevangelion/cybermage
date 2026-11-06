#include "events.h"
#include "ai.h"
#include <string.h>

static int32_t dice(world *w, const events_definition *d)
{
    unsigned i;
    int32_t value = d->dice_add;
    for (i = 0; i < d->dice_count; ++i) value += world_rnd(&w->rng,d->dice_sides) + 1;
    return value;
}

static void notify(events_state *e, int type, int icon)
{
    e->item_up_type = type; e->item_up_icon = icon; e->item_up_time = e->ticks + 80;
}

static void fly_landing(sim_state *s, const world_object *p)
{
    s->flight_mode = 0; s->flight_unk_144B48 = 0; s->unk_144aec = 0;
    s->unk_144b34 = s->unk_144b38 = s->unk_144b3c = 0;
    s->body_height = p->z + p->z / 2 + 0x1000;
}

/* CM_HI.EXE Xox::UseItem 01A85C, FlyLanding 01A7BC. */
int events_use_item(events_state *e, sim_state *s, world *w, unsigned slot)
{
    events_item *v;
    world_person *pc;
    world_object *p;
    int used = 0, consume = 0;
    unsigned i;
    if (!e || !s || !w || slot >= EVENTS_ITEM_MAX || w->player_object >= w->object_count)
        return cm_fail("UseItem", slot, "invalid inventory slot/player");
    v = &e->items[slot]; pc = &w->persons[e->foot_person]; p = &w->objects[w->player_object];
    switch (v->kind) {
    case 9:
        if ((int16_t)pc->unk_0038 < (int16_t)((uint32_t)pc->unk_0038 >> 16)) {
            uint32_t maximum = (uint32_t)pc->unk_0038 >> 16;
            pc->unk_0038 = (int32_t)(maximum << 16 | maximum); used = 1; consume = 1;
        }
        break;
    case 11:
        for (i = 0; i < EVENTS_ARMOR_MAX; ++i)
            if (e->armor[i] > 0 && e->armor[i] < e->max_armor[i]) {
                e->armor[i] = e->max_armor[i]; used = 1; consume = 1;
            }
        break;
    case 14:
        if (v->type == 22) { e->spell = 5; s->pc_mode = 5; used = 1; }
        else if (v->type == 111) {
            if (s->flight_mode) fly_landing(s,p);
            else { s->flight_mode = 3; s->flight_active = 1; s->flight_unk_144B48 = 0;
                s->unk_144b34 = s->unk_144b38 = s->unk_144b3c = 0; s->body_height = p->z * 2; used = 1; }
        } else {
            int result = events_key_search(e,s,w,v->subtype);
            if (result < 0) return -1;
            used = result;
        }
        break;
    case 0:
        if (v->type == 25) {
            if (s->flight_mode) fly_landing(s,p);
            else { s->flight_mode = 1; s->flight_active = 1; s->flight_unk_144B48 = 0;
                s->unk_144b34 = s->unk_144b38 = s->unk_144b3c = 0; s->body_height = p->z * 2; used = 1; }
        } else if (v->type == 104 && w->scene == 9 && w->person_count > 11 && w->persons[11].status != 2) {
            world_object *car = &w->objects[w->persons[11].object];
            world_omap_delete(w,(unsigned)w->persons[11].object);
            car->x = 0xB8000;
            if (cm_distance(p->x,p->y,0xB8000,0x68000) > 0x18000) { car->y = 0x68000; car->floor_z = 0x22000; }
            else { car->y = 0x88000; car->floor_z = 0x20000; }
            car->heading = 0; world_omap_insert(w,(unsigned)w->persons[11].object); used = 1;
        } else if (v->type == 120) e->recorder_requested = !e->text_trigger;
        break;
    default: break;
    }
    if (used) notify(e,v->type,v->object);
    if (consume && v->quantity > 0) { --v->quantity; if (!v->quantity) v->type = 0; }
    return 0;
}

static int16_t add_limit(int16_t value, int32_t add, int16_t maximum)
{
    int16_t result = (int16_t)(uint16_t)((uint32_t)(uint16_t)value + (uint32_t)add);
    return result > maximum ? maximum : result;
}

static int grant_item(events_state *e, world *w, unsigned type, int object,
                       unsigned depth)
{
    unsigned i, slot = EVENTS_ITEM_MAX, subtype;
    const events_definition *d;
    world_person *pc;
    int32_t amount;
    int keep = 1, take = 1;
    if(depth>=256) return cm_fail("CollItem",type,"cyclic item bundle");
    if (!type || type > e->definition_count) return cm_fail("CollItem",type,"invalid item definition");
    d = &e->definitions[type]; subtype = d->subtype;
    for (i = 0; i < EVENTS_ITEM_MAX; ++i) if (e->items[i].type == (int)type) { slot = i; break; }
    if (d->kind == 6)
        for (i = 0; i < EVENTS_ITEM_MAX; ++i)
            if (e->items[i].type > 0 && e->items[i].kind == 6 && e->items[i].subtype == subtype &&
                (subtype != 7 || e->items[i].variant == d->spell)) slot = i;
    if (slot == EVENTS_ITEM_MAX)
        for (i = 0; i < EVENTS_ITEM_MAX; ++i) if (e->items[i].type <= 0) { slot = i; break; }
    if (slot == EVENTS_ITEM_MAX) return 0;
    amount = dice(w,d); pc = &w->persons[e->foot_person];
    switch (d->kind) {
    case 1: case 2: case 3: case 4: {
        static const uint8_t part[5] = {0,3,2,1,0};
        unsigned part_index = part[d->kind];
        take = 0; keep = 0;
        if (e->max_armor[part_index] < (int)subtype) {
            e->max_armor[part_index] = e->armor[part_index] = (int16_t)subtype; take = 1;
        } else if (e->armor[part_index] < e->max_armor[part_index]) {
            e->armor[part_index] = add_limit(e->armor[part_index],subtype,e->max_armor[part_index]); take = 1;
        }
        if (d->kind == 3 || d->kind == 4) {
            part_index = d->kind == 3 ? 4 : 5;
            if (e->max_armor[part_index] < (int)subtype) { e->max_armor[part_index] = e->armor[part_index] = (int16_t)subtype; take = 1; }
            else if (e->armor[part_index] < e->max_armor[part_index]) { e->armor[part_index] = add_limit(e->armor[part_index],subtype,e->max_armor[part_index]); take = 1; }
        }
        break;
    }
    case 6:
        if (subtype != 7 && subtype < 11) {
            static const uint8_t ammo_type[11] = {0,1,2,3,4,5,6,11,7,8,7};
            unsigned ammo = ammo_type[subtype];
            if (subtype >= 8) {
                if (e->items[slot].quantity >= e->max_ammo[ammo]) take = 0;
            } else {
                if (e->items[slot].type > 0 && e->ammo[ammo] >= e->max_ammo[ammo]) take = 0;
                else e->ammo[ammo] = add_limit(e->ammo[ammo],dice(w,d),e->max_ammo[ammo]);
            }
        }
        break;
    case 7: e->unk_1443FC = subtype == 1 ? 1 : e->unk_1443FC; e->unk_144400 = subtype == 2 ? 1 : e->unk_144400; break;
    case 8:
        if (subtype >= 12) return cm_fail("CollItem",type,"invalid ammo type");
        keep = 0; take = e->ammo[subtype] < e->max_ammo[subtype];
        if (take) e->ammo[subtype] = add_limit(e->ammo[subtype],dice(w,d),e->max_ammo[subtype]);
        break;
    case 9: case 11: amount = 1; break;
    case 10: {
        int16_t hp = (int16_t)pc->unk_0038, max = (int16_t)((uint32_t)pc->unk_0038 >> 16);
        keep = 0; take = hp < max;
        if (take) pc->unk_0038 = (int32_t)((uint32_t)(uint16_t)max << 16 | (uint16_t)add_limit(hp,amount,max));
        break;
    }
    case 12:
        keep = 0;
        for (i = 0; i < EVENTS_ARMOR_MAX; ++i) if (i != 2) {
            if (e->armor[i] <= 0) e->armor[i] = e->max_armor[i] = 5;
            else if (e->armor[i] < e->max_armor[i]) e->armor[i] = add_limit(e->armor[i],dice(w,d),e->max_armor[i]);
        }
        break;
    case 13:
        if (d->spell < 0 || d->spell >= 64) return cm_fail("CollItem",type,"invalid spell");
        e->spells[(unsigned)d->spell / 8] |= (uint8_t)(1u << ((unsigned)d->spell % 8)); keep = 0;
        break;
    case 14:
        if(object>=0) e->definitions[type].subtype = (uint8_t)w->objects[object].heading;
        subtype = e->definitions[type].subtype; break;
    case 15: e->cash = (int32_t)((uint32_t)e->cash + (uint32_t)dice(w,d)); keep = 0; break;
    case 16: {
        unsigned mask=d->subtype&15u,choice;
        int32_t effect=dice(w,d);
        int16_t hp=(int16_t)pc->unk_0038,max=(int16_t)((uint32_t)pc->unk_0038>>16);
        if(d->flags&4) effect=-effect;
        if(!mask) mask=1;
        choice=(unsigned)world_rnd(&w->rng,4);
        for(i=0;i<4 && !(mask&(1u<<choice));++i) choice=(choice+1)%4;
        if(choice==0 && (mask&2) && hp>=max) choice=e->energy<e->max_energy?1:2;
        if(choice==1 && (mask&1) && e->energy>=e->max_energy) choice=hp<max?0:3;
        if(choice==0) {
            if(effect<0) {
                if(!e->actors) return cm_fail("CollItem",type,"damage owner not bound");
                if(ai_damage_person(e->actors,w,w->player_person,-effect*10,0,0)) return -1;
            } else pc->unk_0038=(int32_t)((uint32_t)(uint16_t)max<<16 | (uint16_t)add_limit(hp,effect*10,max));
        } else if(choice==1) {
            int32_t energy=(int16_t)(uint16_t)((uint32_t)e->energy+(uint32_t)effect*10u);
            if(energy<0) energy=0;
            if(energy>e->max_energy) energy=e->max_energy;
            e->energy=energy;
        } else if(choice==2) {
            max=(int16_t)(uint16_t)((uint32_t)(uint16_t)max+(uint32_t)effect);
            if(max>999) max=999;
            hp=add_limit(hp,effect,max);
            pc->unk_0038=(int32_t)((uint32_t)(uint16_t)max<<16|(uint16_t)hp);
        } else {
            e->max_energy=(int16_t)(uint16_t)((uint32_t)e->max_energy+(uint32_t)effect);
            if(e->max_energy>999) e->max_energy=999;
            e->energy=add_limit((int16_t)e->energy,effect,(int16_t)e->max_energy);
        }
        keep=0;break;
    }
    case 17: {
        unsigned pick,tries;
        enum { BUNDLE_DRAW_MAX=65536 };
        int nonempty=0;
        if(object>=0) { world_omap_delete(w,(unsigned)object);w->objects[object].active=0; }
        for(i=0;i<6;++i) if(d->extra[i]) nonempty=1;
        if(!nonempty) return cm_fail("CollItem",type,"empty item bundle");
        pick=0;
        for(tries=0;tries<BUNDLE_DRAW_MAX;++tries) {
            pick=(unsigned)world_rnd(&w->rng,6);
            if((int8_t)d->extra[pick]) break;
        }
        if(tries==BUNDLE_DRAW_MAX) return cm_fail("CollItem",type,"item bundle draw limit reached");
        if(grant_item(e,w,(unsigned)(int8_t)d->extra[pick],-1,depth+1) ||
           grant_item(e,w,(unsigned)amount,-1,depth+1) ||
           grant_item(e,w,subtype,-1,depth+1)) return -1;
        return 0;
    }
    default: break;
    }
    if (!take) return 0;
    if (keep) {
        events_item *v = &e->items[slot];
        if (v->type <= 0) v->quantity = 0;
        memcpy(v->name,d->name,sizeof(v->name)); v->type = (int16_t)type; v->object = d->icon;
        v->icon = d->scon; v->kind = d->kind; v->subtype = (uint8_t)subtype;
        v->flags = d->flags; v->variant = (uint8_t)d->spell; v->unk_0024 = d->value;
        v->quantity = (int32_t)((uint32_t)v->quantity + (uint32_t)amount);
    }
    notify(e,(int)type,d->icon);
    if(object>=0) { world_omap_delete(w,(unsigned)object); w->objects[object].active = 0; }
    return 0;
}

/* CollItem042700: targetable2 PROP auxiliary is the item definition id. */
int events_coll_item(events_state *e, world *w, unsigned object, unsigned source)
{
    unsigned i,type;
    world_span props;
    if(!e || !w || object>=w->object_count || source>=w->object_count)
        return cm_fail("CollItem",object,"invalid pickup object");
    if(source!=w->player_object || !w->objects[object].active || w->player_person!=e->foot_person) return 0;
    props=w->scene_sections[1];
    for(i=0;i<w->prop_count;++i) if(w->prop_objects[i]==object) {
        if(props.size<30 || i>=(props.size-30)/30) return cm_fail("CollItem",i,"truncated prop placement");
        type=cm_u16(props.data+(i+1)*30+26);
        return grant_item(e,w,type,(int)object,0);
    }
    return cm_fail("CollItem",object,"pickup has no placement");
}
