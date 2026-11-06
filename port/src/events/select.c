#include "events.h"

/* CM_HI.EXE cmselect.cpp 056DDC/057154/057A0C; docs/re/events.md. */
static int weapon_order(int type)
{
    static const int16_t types[20] = {4,31,34,5,20,42,33,36,24,21,1,69,39,68,161,94,86,71,89,30};
    unsigned i;
    if (type == 142) return 4;
    if (type == 151) return 5;
    for (i = 0; i < 20; ++i) if (types[i] == type) return (int)i;
    return -1;
}

static int key_order(int type)
{
    switch (type) {
    case 120: return 0;
    case 25: return 1;
    case 22: return 2;
    case 6: return 3;
    case 11: return 4;
    default: return type >= 78 && type <= 85 ? 1012 + type - 78 : 100 + type;
    }
}

int events_make_selector(events_state *e, unsigned kind)
{
    static const uint8_t spell_order[16] = {0,3,1,4,2,255,255,5,7,255,255,255,6,255,255,255};
    unsigned i, j, count = 0, other = 5;
    if (!e || kind < 1 || kind > 3) return cm_fail("selector", kind, "invalid selector kind");
    for (i = 0; i < EVENTS_SELECTOR_MAX; ++i) e->selector_keys[i] = -1;
    for (i = 0; i < (kind == 2 ? 64u : EVENTS_ITEM_MAX); ++i) {
        int order, eligible;
        if (kind == 2) {
            if (!(e->spells[i / 8] & (1u << (i % 8)))) continue;
            if (i >= 16 || spell_order[i] >= EVENTS_SELECTOR_MAX)
                return cm_fail("selector/spell", i, "spell has no keyboard selector entry");
            order = spell_order[i]; eligible = 1;
        } else {
            const events_item *v = &e->items[i];
            if (v->type <= 0) continue;
            eligible = kind == 1 ? v->kind == 5 || v->kind == 6 :
                v->kind == 0 || v->kind == 7 || v->kind == 9 || v->kind == 11 || v->kind == 14;
            order = kind == 1 ? weapon_order(v->type) : key_order(v->type);
        }
        if (!eligible) continue;
        if (count == EVENTS_SELECTOR_MAX) return cm_fail("selector", count, "more than twenty entries");
        e->selector_items[count] = (int32_t)i; e->selector_order[count++] = order;
    }
    /* Original compares each position with the shrinking outer end, not qsort. */
    for (j = count; j > 1; --j)
        for (i = 0; i + 1 < j; ++i)
            if (e->selector_order[i] > e->selector_order[j - 1]) {
                int32_t v = e->selector_order[i];
                e->selector_order[i] = e->selector_order[j - 1]; e->selector_order[j - 1] = v;
                v = e->selector_items[i]; e->selector_items[i] = e->selector_items[j - 1]; e->selector_items[j - 1] = v;
            }
    for (i = 0; i < count; ++i) {
        int order = e->selector_order[i];
        if (kind == 3) {
            if (order >= 1000) order -= 1000;
            else if (order >= 100) order = (int)other++;
            e->selector_order[i] = order;
        }
        if (order >= 0 && order < EVENTS_SELECTOR_MAX) e->selector_keys[order] = (int32_t)i;
    }
    e->selector_kind = kind; e->selector_count = count; e->selector_index = 0;
    return 0;
}

static int scan(sim_state *s, unsigned code)
{
    if (!s->kb_scan_status[code]) return 0;
    s->kb_scan_status[code] = 0;
    return 1;
}

int events_process_selector(events_state *e, sim_state *s, world *w)
{
    unsigned col, row, next;
    if (!e || !s || !w || !e->selector_kind) return 0;
    if (scan(s, 1)) { e->selector_kind = 0; s->pc_mode = e->previous_mode; return 0; }
    if (!e->selector_count) return 0;
    col = e->selector_index % 5; row = e->selector_index / 5;
    if (scan(s, 0x4B) && col) --col;
    if (scan(s, 0x4D) && col < 4 && col < e->selector_count - 1) ++col;
    if (scan(s, 0x48) && row) --row;
    if (scan(s, 0x50) && row < 3 && row < (e->selector_count - 1) / 5) ++row;
    next = row * 5 + col;
    if (next < e->selector_count) e->selector_index = next;
    if (scan(s, 0x1C)) {
        unsigned selected = (unsigned)e->selector_items[e->selector_index];
        s->pc_mode = e->previous_mode;
        if (e->selector_kind == 2) { e->spell = (int32_t)selected; s->pc_mode = 5; }
        else if (e->selector_kind == 1) {
            if (e->items[selected].kind == 5) { e->hh_weapon = e->items[selected].subtype; s->pc_mode = 2; }
            else { e->weapon = e->items[selected].subtype; s->pc_mode = 1; }
        } else if (events_use_item(e, s, w, selected)) return -1;
        e->selector_kind = 0;
    }
    return 0;
}
