#include "events.h"
#include <string.h>

/* CM_HI.EXE LoadEVENTS 02732C, LoadSWITCHES 0274F8; docs/re/events.md. */
int events_init(events_state *e, world *w)
{
    world_span span;
    unsigned i, j, count;
    const uint8_t *p;
    static const int16_t ammo_size[12] = {250,400,60,250,150,500,500,100,100,0,0,0};
    if (!e || !w) return cm_fail("events", 0, "missing events/world storage");
    memset(e, 0, sizeof(*e));
    e->energy = e->max_energy = w->scene == 11 ? 100 : 40;
    e->cash = w->scene == 11 ? 60 : 85;
    e->spells[0] = 1;
    for (i = 0; i < EVENTS_ARMOR_MAX; ++i) e->armor[i] = e->max_armor[i] = 30;
    for (i = 0; i < 12; ++i) e->max_ammo[i] = ammo_size[i];
    e->foot_object = w->player_object; e->foot_person = w->player_person;
    span = w->scene_sections[4];
    if (!span.data || span.size < 328)
        return cm_fail("SCENE.RSC/events", span.size, "truncated event header");
    count = cm_u32(span.data + 256);
    if (count >= EVENTS_EVENT_MAX || count > (span.size - 328) / 72)
        return cm_fail("SCENE.RSC/events", 256, "invalid event count");
    e->event_count = count;
    for (i = 0; i < 64; ++i) {
        e->event_heads[i] = cm_u32(span.data + i * 4);
        if (e->event_heads[i] > count)
            return cm_fail("SCENE.RSC/events", i * 4, "invalid event chain head");
    }
    for (i = 1; i <= count; ++i) {
        events_event *v = &e->events[i];
        p = span.data + 256 + i * 72;
        v->next = cm_u16(p + 2); v->type = p[4]; v->state = p[5];
        v->enabled = p[6]; v->once = p[7];
        if (v->next > count) return cm_fail("SCENE.RSC/events", 258 + i * 72, "invalid next event");
        for (j = 0; j < 16; ++j) v->value[j] = (int32_t)cm_u32(p + 8 + j * 4);
        if (v->type == 50) v->state = (uint8_t)v->value[11];
    }
    span = w->scene_sections[5];
    if (!span.data || span.size < 16)
        return cm_fail("SCENE.RSC/switches", span.size, "truncated switch header");
    count = cm_u32(span.data);
    if (count >= EVENTS_SWITCH_MAX || count > (span.size - 16) / 16)
        return cm_fail("SCENE.RSC/switches", 0, "invalid switch count");
    e->switch_count = count;
    for (i = 0; i < count; ++i) {
        events_switch *v = &e->switches[i];
        p = span.data + (i + 1) * 16;
        v->tile = (int32_t)cm_u32(p); v->fxo = (int16_t)cm_u16(p + 8);
        v->link = (int16_t)cm_u16(p + 10); v->key = (int16_t)cm_u16(p + 12);
        v->enabled = p[14]; v->state = p[15];
        if (v->state > 1 || v->fxo < 0 || v->link >= (int)count || v->link <= -(int)count)
            return cm_fail("SCENE.RSC/switches", (i + 1) * 16, "invalid switch record");
        for (j = 0; j < 2; ++j) {
            int object = world_fxo_object(w, cm_u16(p + 4 + j * 2));
            if (object < 0) return cm_fail("SCENE.RSC/switches", (i + 1) * 16 + 4 + j * 2, "invalid switch texture object");
            v->textures[j] = w->objects[object].texture;
        }
    }
    {
        const uint8_t *data;
        uint32_t size;
        if (rsc_entry(&w->scenes, 101, &data, &size)) return -1;
        p = data;
        if (size < 154) return cm_fail("SCENE.RSC/items", size, "truncated item definitions");
        count = cm_u32(p);
        if (count >= 172 || count > (size - 154) / 154)
            return cm_fail("SCENE.RSC/items", 0, "invalid item count");
        e->definition_count = count;
        for (i = 1; i <= count; ++i) {
            events_definition *v = &e->definitions[i];
            p = data + i * 154;
            memcpy(v->name, p, 19); v->name[19] = 0;
            v->kind = p[20]; v->subtype = p[21]; v->icon = p[22]; v->flags = p[23];
            v->unk_0018 = (int32_t)cm_u32(p + 142);
            v->scon = (int16_t)cm_u16(p + 146); v->unk_0020 = (int16_t)cm_u16(p + 32);
            v->spell = p[24]; v->value = (int32_t)cm_u32(p + 34);
            v->dice_count = p[150]; v->dice_sides = p[151]; v->dice_add = (int16_t)cm_u16(p + 152);
            memcpy(v->extra, p + 100, 6);
            v->extra[6] = p[79]; v->extra[7] = p[26]; v->extra[8] = p[31];
        }
    }
    /* InitPC gives the speech recorder, not an invented default weapon. */
    if (e->definition_count >= 120) {
        events_item *v = &e->items[0]; const events_definition *d = &e->definitions[120];
        memcpy(v->name, d->name, sizeof(v->name)); v->type = 120; v->object = d->icon;
        v->kind = d->kind; v->subtype = d->subtype; v->icon = d->scon; v->quantity = -1;
    }
    return 0;
}

int events_find_auto_slot(const events_state *e)
{
    unsigned i;
    for (i = 0; i < EVENTS_AUTO_MAX; ++i) if (!e->auto_slot[i]) return (int)i;
    return -1;
}
