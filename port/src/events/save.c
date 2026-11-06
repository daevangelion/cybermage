#include "events.h"
#include "audio.h"
#include <string.h>

#define INVENTORY_BASE 0x130D50u
static uint16_t get16(const ui_save_state *s, unsigned offset)
{
    return s->unk_130D50[(offset-INVENTORY_BASE)/2];
}
static uint32_t get32(const ui_save_state *s, unsigned offset)
{
    return get16(s,offset) | (uint32_t)get16(s,offset+2) << 16;
}
static void put16(ui_save_state *s, unsigned offset, uint16_t value)
{
    s->unk_130D50[(offset-INVENTORY_BASE)/2] = value;
}
static void put32(ui_save_state *s, unsigned offset, uint32_t value)
{
    put16(s,offset,(uint16_t)value); put16(s,offset+2,(uint16_t)(value>>16));
}

/* The surrounding SAVE codec owns bytes; never cast a native inventory record. */
int events_save_import(events_state *e, const ui_save_state *save)
{
    unsigned i, j;
    if (!e || !save) return cm_fail("events/save",0,"missing save storage");
    for (i = 0; i < EVENTS_ITEM_MAX; ++i) {
        events_item *v = &e->items[i]; unsigned at = 0x130D64+i*40;
        for (j = 0; j < 20; j+=2) { uint16_t word = get16(save,at+j); v->name[j]=(char)(uint8_t)word; v->name[j+1]=(char)(uint8_t)(word>>8); }
        v->type=(int16_t)get16(save,at+20); v->object=(int16_t)get16(save,at+22);
        v->icon=(int32_t)get32(save,at+24); v->quantity=(int32_t)get32(save,at+28);
        v->kind=(uint8_t)get16(save,at+32); v->subtype=(uint8_t)(get16(save,at+32)>>8);
        v->flags=(uint8_t)get16(save,at+34); v->variant=(uint8_t)(get16(save,at+34)>>8);
        v->unk_0024=(int32_t)get32(save,at+36);
        if (v->type < 0 || (v->type > 0 && (unsigned)v->type > e->definition_count)) return cm_fail("events/save",at,"invalid saved item type");
    }
    e->cash=(int32_t)get32(save,0x1313A4); e->spell=(int32_t)get32(save,0x1313A8);
    e->hh_weapon=(int32_t)get32(save,0x1313AC); e->weapon=(int32_t)get32(save,0x1313B0);
    e->item_spell=(int32_t)get32(save,0x1313B4);e->weapon_slot=get32(save,0x1313B8);e->hh_dice=get32(save,0x1313BC);
    if(e->weapon_slot>=EVENTS_ITEM_MAX) return cm_fail("events/save",0x1313B8,"invalid selected weapon slot");
    e->PCBet=(int32_t)save->PCBet;e->PCBetPick=(int32_t)save->PCBetPick;
    e->PCBetOdds=(int32_t)save->PCBetOdds;e->JackPot=(int32_t)save->JackPot;
    e->autoDestruct=(int32_t)save->autoDestruct;e->autoDTime=e->ticks+save->autoDTime;
    e->energy=(int16_t)get16(save,0x131410); e->max_energy=(int16_t)get16(save,0x131412);
    for (i=0;i<8;++i) e->spells[i]=(uint8_t)(get16(save,0x1313C0+(i&~1u)) >> (i%2*8));
    for (i=0;i<EVENTS_ARMOR_MAX;++i) { e->armor[i]=(int16_t)get16(save,0x1313C8+i*2); e->max_armor[i]=(int16_t)get16(save,0x1313D4+i*2); }
    for (i=0;i<12;++i) { e->ammo[i]=(int16_t)get16(save,0x1313E0+i*2); e->max_ammo[i]=(int16_t)get16(save,0x1313F8+i*2); }
    for (i=0;i<EVENTS_EVENT_MAX;++i) {
        e->events[i].state=(uint8_t)(save->unk_139390[i/2] >> (i%2*8));
        e->events[i].enabled=(uint8_t)(save->unk_139590[i/2] >> (i%2*8));
        for (j=0;j<16;++j) e->events[i].value[j]=(int32_t)save->unk_139D90[i*16+j];
    }
    for (i=0;i<EVENTS_SWITCH_MAX;++i) {
        e->switches[i].state=save->unk_142B78[i]; e->switches[i].key=(int16_t)cm_u16(save->unk_1428F8+i*2);
        if (e->switches[i].state>1) return cm_fail("events/save",0x142B78+i,"invalid saved switch state");
    }
    e->text_trigger=(int32_t)save->textTrigger; e->text_object=(int32_t)save->textObj; e->text_message=(int32_t)save->textMsgNum;
    e->text_switch1=(int32_t)save->textSwitch1; e->text_switch0=(int32_t)save->textSwitch0; e->text_event=(int32_t)save->textEvent;
    e->unk_1443FC=(int32_t)save->unk_1443FC; e->unk_144400=(int32_t)save->unk_144400;
    memcpy(e->speechLog,save->speechLog,sizeof(e->speechLog));
    memcpy(e->coderStr,save->coderStr,sizeof(e->coderStr));snd_code(e->coderStr);
    return 0;
}

int events_save_export(const events_state *e, ui_save_state *save)
{
    unsigned i,j;
    if (!e || !save) return cm_fail("events/save",0,"missing save storage");
    for (i=0;i<EVENTS_ITEM_MAX;++i) {
        const events_item *v=&e->items[i]; unsigned at=0x130D64+i*40;
        for (j=0;j<20;j+=2) put16(save,at+j,(uint16_t)((uint8_t)v->name[j] | (uint16_t)(uint8_t)v->name[j+1]<<8));
        put16(save,at+20,(uint16_t)v->type); put16(save,at+22,(uint16_t)v->object);
        put32(save,at+24,(uint32_t)v->icon); put32(save,at+28,(uint32_t)v->quantity);
        put16(save,at+32,(uint16_t)(v->kind | (uint16_t)v->subtype<<8)); put16(save,at+34,(uint16_t)(v->flags | (uint16_t)v->variant<<8));
        put32(save,at+36,(uint32_t)v->unk_0024);
    }
    put32(save,0x1313A4,(uint32_t)e->cash); put32(save,0x1313A8,(uint32_t)e->spell);
    put32(save,0x1313AC,(uint32_t)e->hh_weapon); put32(save,0x1313B0,(uint32_t)e->weapon);
    put32(save,0x1313B4,(uint32_t)e->item_spell);put32(save,0x1313B8,e->weapon_slot);put32(save,0x1313BC,e->hh_dice);
    save->PCBet=(uint32_t)e->PCBet;save->PCBetPick=(uint32_t)e->PCBetPick;
    save->PCBetOdds=(uint32_t)e->PCBetOdds;save->JackPot=(uint32_t)e->JackPot;
    save->autoDestruct=(uint32_t)e->autoDestruct;save->autoDTime=e->autoDTime-e->ticks;
    put16(save,0x131410,(uint16_t)e->energy); put16(save,0x131412,(uint16_t)e->max_energy);
    for (i=0;i<8;i+=2) put16(save,0x1313C0+i,(uint16_t)(e->spells[i] | (uint16_t)e->spells[i+1]<<8));
    for (i=0;i<EVENTS_ARMOR_MAX;++i) { put16(save,0x1313C8+i*2,(uint16_t)e->armor[i]); put16(save,0x1313D4+i*2,(uint16_t)e->max_armor[i]); }
    for (i=0;i<12;++i) { put16(save,0x1313E0+i*2,(uint16_t)e->ammo[i]); put16(save,0x1313F8+i*2,(uint16_t)e->max_ammo[i]); }
    for (i=0;i<EVENTS_EVENT_MAX;i+=2) {
        save->unk_139390[i/2]=(uint16_t)(e->events[i].state | (uint16_t)e->events[i+1].state<<8);
        save->unk_139590[i/2]=(uint16_t)(e->events[i].enabled | (uint16_t)e->events[i+1].enabled<<8);
    }
    for (i=0;i<EVENTS_EVENT_MAX;++i) for(j=0;j<16;++j) save->unk_139D90[i*16+j]=(uint32_t)e->events[i].value[j];
    for (i=0;i<EVENTS_SWITCH_MAX;++i) {
        save->unk_142B78[i]=e->switches[i].state; save->unk_1428F8[i*2]=(uint8_t)e->switches[i].key; save->unk_1428F8[i*2+1]=(uint8_t)((uint16_t)e->switches[i].key>>8);
    }
    save->textTrigger=(uint32_t)e->text_trigger; save->textObj=(uint32_t)e->text_object; save->textMsgNum=(uint32_t)e->text_message;
    save->textSwitch1=(uint32_t)e->text_switch1; save->textSwitch0=(uint32_t)e->text_switch0; save->textEvent=(uint32_t)e->text_event;
    save->unk_1443FC=(uint32_t)e->unk_1443FC; save->unk_144400=(uint32_t)e->unk_144400;
    memcpy(save->speechLog,e->speechLog,sizeof(e->speechLog));
    memcpy(save->coderStr,e->coderStr,sizeof(e->coderStr));
    return 0;
}
