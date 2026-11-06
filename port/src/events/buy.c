#include "events.h"
#include <string.h>

/* BuyItem041F80 is a grant, never pricing or a cash debit. Returns1/0/-1. */
static int inventory_slot(const events_state *e, unsigned type)
{
    const events_definition *d=&e->definitions[type];
    int slot=-1;
    unsigned i;
    for(i=0;i<EVENTS_ITEM_MAX;++i) if(e->items[i].type==(int)type) { slot=(int)i; break; }
    if(d->kind==6)
        for(i=0;i<EVENTS_ITEM_MAX;++i)
            if(e->items[i].type>0 && e->items[i].kind==6 && e->items[i].subtype==d->subtype &&
               (d->subtype!=7 || e->items[i].variant==d->spell)) slot=(int)i;
    if(slot<0) for(i=0;i<EVENTS_ITEM_MAX;++i) if(e->items[i].type<=0) { slot=(int)i; break; }
    return slot;
}

static void store(events_state *e, unsigned slot, unsigned type, int32_t amount)
{
    const events_definition *d=&e->definitions[type];
    events_item *v=&e->items[slot];
    if(v->type<=0) v->quantity=0;
    memcpy(v->name,d->name,sizeof(v->name));
    v->type=(int16_t)type; v->object=d->icon; v->icon=d->scon;
    v->kind=d->kind; v->subtype=d->subtype; v->flags=d->flags;
    v->variant=(uint8_t)d->spell; v->unk_0024=d->value;
    v->quantity=(int32_t)((uint32_t)v->quantity+(uint32_t)amount);
}

int events_inventory_add(events_state *e, unsigned type, int32_t amount)
{
    int slot;
    if(!e || !type || type>e->definition_count) return cm_fail("inventory/add",type,"invalid item definition");
    slot=inventory_slot(e,type);
    if(slot<0) return 0;
    store(e,(unsigned)slot,type,amount);
    return 1;
}

int events_inventory_remove(events_state *e, unsigned slot, int32_t amount)
{
    events_item *v;
    if(!e || slot>=EVENTS_ITEM_MAX || amount<=0) return cm_fail("inventory/remove",slot,"invalid removal");
    v=&e->items[slot];
    if(v->type<=0) return 0;
    if(v->quantity<=0 || amount>=v->quantity) { v->quantity=0; v->type=0; }
    else v->quantity-=amount;
    return 1;
}
int events_inventory_sell(events_state *e, unsigned slot, int32_t amount)
{
    return events_inventory_remove(e,slot,amount);
}
void events_cash_add(events_state *e, int32_t delta)
{
    e->cash=(int32_t)((uint32_t)e->cash+(uint32_t)delta);
}

static int16_t add_max(int16_t v, int32_t amount, int16_t max)
{
    int16_t n=(int16_t)(uint16_t)((uint32_t)(uint16_t)v+(uint32_t)amount);
    return n>max?max:n;
}

int events_buy_item(events_state *e, world *w, unsigned type)
{
    const events_definition *d;
    int slot,keep=1;
    int32_t amount;
    unsigned i,ammo=0;
    static const uint8_t ammo_type[11]={0,1,2,3,4,5,6,11,7,8,7};
    if(!e || !w || !type || type>e->definition_count || e->foot_person>=w->person_count)
        return cm_fail("BuyItem",type,"invalid item/player");
    d=&e->definitions[type]; slot=inventory_slot(e,type);
    if(slot<0) return 0;
    amount=d->dice_count*d->dice_sides+d->dice_add;
    switch(d->kind) {
    case 1: case 2: case 3: case 4: {
        static const uint8_t part[5]={0,3,2,1,0};
        unsigned first=part[d->kind],second=d->kind==3?4:5;
        int changed=0;
        keep=0;
        if(e->max_armor[first]<d->subtype) { e->max_armor[first]=e->armor[first]=d->subtype; changed=1; }
        if(d->kind==3 || d->kind==4)
            if(e->max_armor[second]<d->subtype) { e->max_armor[second]=e->armor[second]=d->subtype; changed=1; }
        if(!changed) return 0;
        break;
    }
    case 5: if(e->items[slot].type>0) return 0; break;
    case 6:
        if(d->subtype>=11) return cm_fail("BuyItem",type,"invalid weapon ammo type");
        ammo=ammo_type[d->subtype];
        if(d->subtype>=8) { if(ammo && e->items[slot].quantity>=e->max_ammo[ammo]) return 0; }
        else {
            if(e->items[slot].type>0) return 0;
            e->ammo[ammo]=add_max(e->ammo[ammo],amount,e->max_ammo[ammo]);
        }
        break;
    case 7:
        if(d->subtype==1) { if(e->unk_1443FC) return 0; e->unk_1443FC=1; }
        if(d->subtype==2) { if(e->unk_144400) return 0; e->unk_144400=1; }
        break;
    case 8:
        ammo=d->subtype;
        if(ammo>=12) return cm_fail("BuyItem",type,"invalid ammo type");
        if(e->ammo[ammo]>=e->max_ammo[ammo]) return 0;
        e->ammo[ammo]=add_max(e->ammo[ammo],amount,e->max_ammo[ammo]); keep=0;
        break;
    case 9: case 11: amount=1; break;
    case 10: {
        world_person *p=&w->persons[e->foot_person];
        int16_t hp=(int16_t)p->unk_0038,max=(int16_t)((uint32_t)p->unk_0038>>16);
        if(hp>=max) return 0;
        p->unk_0038=(int32_t)((uint32_t)(uint16_t)max<<16 | (uint16_t)add_max(hp,amount,max)); keep=0;
        break;
    }
    case 12:
        keep=0;
        for(i=0;i<EVENTS_ARMOR_MAX;++i) if(i!=2) {
            if(e->armor[i]<=0) e->armor[i]=e->max_armor[i]=5;
            else if(e->armor[i]<e->max_armor[i]) e->armor[i]=add_max(e->armor[i],amount,e->max_armor[i]);
        }
        break;
    case 18: {
        world_person *car;
        int16_t hp,max;
        keep=0;
        if(w->scene!=9 || w->person_count<=11) return 0;
        car=&w->persons[11]; car->status=0; car->state=13;
        hp=(int16_t)car->unk_0038; max=(int16_t)((uint32_t)car->unk_0038>>16);
        if(hp>=max) return 0;
        car->unk_0038=(int32_t)((uint32_t)(uint16_t)max<<16 | (uint16_t)max);
        break;
    }
    default: break;
    }
    if(keep) {
        store(e,(unsigned)slot,type,amount);
        if(ammo && e->items[slot].quantity>e->max_ammo[ammo]) e->items[slot].quantity=e->max_ammo[ammo];
    }
    e->item_up_type=(int32_t)type; e->item_up_icon=d->icon; e->item_up_time=e->ticks+80;
    return 1;
}
