#include "events.h"
#include "ai.h"
#include "audio.h"
#include <string.h>

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
    if(kind!=EVENTS_SELECT_KEY)
        for(i=0;i<EVENTS_SELECTOR_MAX;++i) e->selector_keys[i]=-1;
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
    for(i=0;i<3;++i) e->selectors[i].count=i==kind-1?count:0;
    if(e->selectors[kind-1].index>=count) e->selectors[kind-1].index=0;
    e->selector_kind=kind;e->selector_joy_latch=1;
    return 0;
}

static int scan(sim_state *s, unsigned code)
{
    if (!s->kb_scan_status[code]) return 0;
    s->kb_scan_status[code] = 0;
    return 1;
}

void events_reset_selectors(events_state *e)
{
    unsigned i;
    for(i=0;i<3;++i) { e->selectors[i].count=0;e->selectors[i].index=UINT32_MAX; }
}

int events_selector_needs_input(const events_state *e)
{
    return e && e->selector_kind>=1 && e->selector_kind<=3 &&
        e->selectors[e->selector_kind-1].count>0;
}

int events_keyboard(events_state *e, sim_state *s, ai_state *a, world *w)
{
    static const uint8_t keys[EVENTS_SELECTOR_MAX]={
        0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44,
        0x57,0x58,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09
    };
    unsigned i,request=0;
    int shortcut=-1;
    events_selector *q;
    if(!e || !s || !a || !w || e->selector_kind>3)
        return cm_fail("selector",0,"invalid keyboard selector state");
    e->actors=a;
    if(!e->selector_kind) {
        if(s->pc_mode!=1 && s->pc_mode!=2 && s->pc_mode!=5) return 0;
        for(i=0;i<3;++i) if(scan(s,0x3B+i)) request=i+1;
        if(!request) return 0;
        s->previous_pc_mode=s->pc_mode;s->pc_mode=3;
        e->selector_request=request;
        a->pc.cooldown=a->pc.latch=0;a->pc.key_swing_sword=1;
        return ai_reset_aim(a,s);
    }
    q=&e->selectors[e->selector_kind-1];
    if(scan(s,1)) {
        memset(s->kb_scan_status,0,sizeof s->kb_scan_status);
        s->pc_mode=s->previous_pc_mode;s->movement_angle=0;
        for(i=0;i<3;++i) e->selectors[i].count=0;
        e->selector_kind=e->selector_confirm=0;
        if(ai_pc_select(a,s,w,0)) return -1;
        return 0;
    }
    if(!q->count) return 0;
    /* HandleKeyboardEvents01340D..013548: scan order, not a Shift bank.
     * Every pressed shortcut is cleared; the last scanned press wins. */
    if(scan(s,0x1C)) {
        memset(s->kb_scan_status,0,sizeof s->kb_scan_status);
        e->selector_confirm=1;
    }
    else for(i=0;i<EVENTS_SELECTOR_MAX;++i) if(scan(s,keys[i])) shortcut=(int)i;
    if(shortcut>=0 && e->selector_keys[shortcut]>=0) {
        unsigned next=(unsigned)e->selector_keys[shortcut];
        if(next>=q->count && e->selector_kind!=EVENTS_SELECT_KEY)
            return cm_fail("selector",(uint32_t)shortcut,"shortcut outside selector list");
        q->index=next;e->selector_confirm=1;
    }
    if(e->selector_confirm) a->pc.key_swing_sword=1;
    return 0;
}

int events_process_selector(events_state *e, sim_state *s, world *w)
{
    events_selector *q;
    unsigned kind;
    int32_t col,row,next,old,count;
    int changed,confirm;
    int32_t x,y;
    if(!e || !s || !w || !e->selector_kind) return 0;
    if(e->selector_kind>3 || !e->actors)
        return cm_fail("selector",e->selector_kind,"invalid active selector");
    q=&e->selectors[e->selector_kind-1];
    if(!q->count || q->count>EVENTS_SELECTOR_MAX ||
       (e->selector_kind!=EVENTS_SELECT_KEY && q->index>=q->count))
        return cm_fail("selector",q->count,"invalid selector extent/index");
    old=world_wrap32(q->index);count=(int32_t)q->count;col=old%5;row=old/5;
    confirm=e->selector_confirm || e->actors->pc.fire;
    if (scan(s, 0x4B) && col>0) --col;
    if (scan(s, 0x4D) && col < 4 && col < count - 1) ++col;
    if (scan(s, 0x48) && row>0) --row;
    if (scan(s, 0x50) && row < 3 && row < (count - 1) / 5) ++row;
    changed=world_add32(world_mul32(row,5),col)!=old;
    if (!changed && s->selector_pointer) {
        x = world_sub32(s->selector_x, 14);
        y = world_sub32(s->selector_y, 128);
        if (x >= 0 && x < 5 * 116 && y >= 0 && y < 4 * 88 &&
            x % 116 < 112 && y % 88 < 76 && (y / 88) * 5 + x / 116 < count) {
            col = x / 116;
            row = y / 88;
        } else if (s->selector_tap) {
            confirm = 0;
            e->selector_confirm = 0;
        }
    } else if(!changed && s->mouse_present) {
        if(confirm && !s->selector_pointer) changed=1;
        else {
            x=s->mouse_hand_x;y=world_sub32(100,s->mouse_hand_y);
            col=x<-48?0:x<2?1:x<50?2:x<90?3:4;
            row=y>134?0:y>91?1:y>47?2:3;
            if(row>(count-1)/5) row=(count-1)/5;
            if(row*5+col>=count) col=(count-1)%5;
        }
    } else if(!changed && s->joystick_present) {
        x=e->actors->pc.hand_x;y=e->actors->pc.hand_y;
        if(e->selector_joy_latch) {
            if(x>-16 && x<16 && y>-16 && y<16) e->selector_joy_latch=0;
        } else {
            if(x<-32 && col>0) --col;
            else if(x>32 && col<4 && col<count-1) ++col;
            if(y>32 && row>0) --row;
            else if(y<-32 && row<3 && row<(count-1)/5) ++row;
        }
    }
    x=world_sub32(world_mul32(col,54),92);y=world_sub32(140,world_mul32(row,44));
    if(s->mouse_present || s->selector_pointer) {
        if(changed) platform_mouse_warp(world_mul32(world_add32(160,x),2),
                                       world_mul32(world_sub32(200,y),2));
        else { x=s->mouse_hand_x;y=world_sub32(100,s->mouse_hand_y); }
    }
    ai_pc_glove_aim(e->actors,x,y);
    next=world_add32(world_mul32(row,5),col);
    if(next<count && next!=old) { q->index=(uint32_t)next;e->selector_joy_latch=1; }
    if(confirm || world_wrap32(q->index)!=old) (void)snd_play(5,32767,32768);
    if (confirm) {
        kind=e->selector_kind;
        s->pc_mode=s->previous_pc_mode;s->movement_angle=0;
        /*1E4D4: a retained key-map entry may no longer name a live list item.
         * Skip only UseItem; the complete close/reset path still executes. */
        if(kind!=EVENTS_SELECT_KEY || (world_wrap32(q->index)>=0 && q->index<q->count)) {
            unsigned selected = (unsigned)e->selector_items[q->index];
            if(selected>=(kind==EVENTS_SELECT_POWER?64u:EVENTS_ITEM_MAX))
                return cm_fail("selector",selected,"invalid selected item");
            if (kind == 2) { e->spell = (int32_t)selected; s->pc_mode = 5; }
            else if (kind == 1) {
                if (e->items[selected].kind == 5) {
                    e->hh_weapon=e->items[selected].subtype;e->hh_dice=(uint32_t)e->items[selected].unk_0024;s->pc_mode=2;
                } else {
                    e->weapon=e->items[selected].subtype;e->item_spell=e->items[selected].variant;
                    e->weapon_slot=selected;s->pc_mode=1;
                }
            } else if (events_use_item(e, s, w, selected)) return -1;
        }
        if(ai_pc_select(e->actors,s,w,kind)) return -1;
        q->count=0;e->selector_kind=e->selector_confirm=0;
    }
    return 0;
}
