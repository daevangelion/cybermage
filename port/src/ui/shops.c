#include "ui.h"
#include "events.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* CM_HI.EXE SetShopItems48698 and sell dispatch4878C/49F43. */
static int shop_items(cm_ui *ui, const events_state *e)
{
    unsigned i;
    ui->shop_count = 0;
    if (!ui->shop_selling || ui->shop_kind == 6) {
        unsigned bank = ui->shop_kind == 5 ? 1 : ui->shop_kind == 6 ? 2 : 0;
        ui->shop_count = bank == 2 ? 3 : 16;
        for (i = 0; i < ui->shop_count; ++i) {
            int type = ui->shop_selling ? 107+(int)i : ui->shop_stock[bank][i];
            if (type <= 0 || (unsigned)type > e->definition_count)
                return cm_fail("shop", (uint32_t)type, "stock exceeds definitions");
            ui->shop_items[i] = type; ui->shop_slots[i] = -1;
        }
    } else for (i = 0; i < EVENTS_ITEM_MAX; ++i) {
        const events_item *v = &e->items[i];
        if (v->type <= 0) continue;
        if ((unsigned)v->type > e->definition_count)
            return cm_fail("shop", i, "inventory definition exceeds table");
        if (v->kind == 5 || v->kind == 6 || v->kind == 7 ||
            v->kind == 9 || v->kind == 11 ||
            (!v->kind && v->type != 104 && e->definitions[v->type].unk_0018 > 0)) {
            unsigned n = ui->shop_count++;
            ui->shop_items[n] = v->type; ui->shop_slots[n] = (int32_t)i;
        }
    }
    if (ui->shop_selected >= ui->shop_count) ui->shop_selected = 0;
    if (ui->shop_first >= ui->shop_count) ui->shop_first = 0;
    return 0;
}

static int shop_price(const cm_ui *ui, const events_state *e, unsigned index,
                       int32_t *price)
{
    const events_definition *d;
    int32_t cost;
    if (index >= ui->shop_count)
        return cm_fail("shop", index, "selection exceeds stock");
    d = &e->definitions[ui->shop_items[index]];
    cost = d->unk_0018;
    if (cost < 0) return cm_fail("shop", index, "negative item cost");
    if (ui->shop_selling) {
        cost /= 2;
        if (ui->shop_kind != 6) {
            const events_item *v = &e->items[ui->shop_slots[index]];
            int maximum = d->dice_count*d->dice_sides+d->dice_add;
            if (v->kind == 6 && v->subtype > 7 && maximum) {
                int64_t scaled = (int64_t)cost*v->quantity/maximum;
                if (scaled < INT32_MIN || scaled > INT32_MAX)
                    return cm_fail("shop", index, "sale price exceeds cash range");
                cost = (int32_t)scaled;
            }
        }
    }
    *price = cost;
    return 0;
}

static int shop_trade(cm_ui *ui, events_state *e, world *w)
{
    int32_t price;
    unsigned at = ui->shop_selected;
    int granted;
    if (!ui->shop_count) return 0;
    if (shop_price(ui,e,at,&price)) return -1;
    if (!ui->shop_selling) {
        if (e->cash < price) return 0;
        granted = events_buy_item(e,w,(unsigned)ui->shop_items[at]);
        if (granted < 0) return -1;
        if (granted) events_cash_add(e,-price);
    } else if (ui->shop_kind == 6) {
        world_person *p;
        int max;
        if (e->foot_person >= w->person_count)
            return cm_fail("shop", e->foot_person, "missing player health");
        p = &w->persons[e->foot_person];
        max = (int16_t)((uint32_t)p->unk_0038 >> 16);
        if (max <= 5) return 0;
        max -= 5;
        p->unk_0038 = (int32_t)((uint32_t)(uint16_t)max << 16 | (uint16_t)max);
        events_cash_add(e,price);
    } else {
        unsigned slot = (unsigned)ui->shop_slots[at];
        events_item *v = &e->items[slot];
        int32_t amount = v->kind == 9 || v->kind == 11 ? 1 : INT32_MAX;
        if (v->kind == 7) {
            if (v->subtype == 1) e->unk_1443FC = 0;
            if (v->subtype == 2) e->unk_144400 = 0;
        }
        if (events_inventory_sell(e,slot,amount) < 0) return -1;
        events_cash_add(e,price);
        if (shop_items(ui,e)) return -1;
    }
    return 0;
}

static int inside(const cm_input *in, int x, int y, int width, int height)
{
    return in->mouse_x >= x*2 && in->mouse_x <= (x+width)*2 &&
        in->mouse_y >= y*2 && in->mouse_y <= (y+height)*2;
}

static int coder_step(cm_ui *ui, events_state *e, const cm_input *in)
{
    static const unsigned scans[8] = {30,48,46,32,2,3,4,5};
    unsigned i, button = 0;
    size_t length = strlen(ui->shop_code);
    for (i=0;i<8;++i) if (in->pressed[scans[i]]) button = i+1;
    if (in->pressed[28]) button = 9;
    if (in->pressed[14] || in->pressed[83]) button = 10;
    if (in->mouse_pressed & 1u) for (i=1;i<=11;++i) {
        const ui_bitmap *b = &ui->shop_gfx[2][i-1];
        if (inside(in,ui->coder_x[i],ui->coder_y[i],b->width,b->height)) button = i;
    }
    if (!button) return 1;
    ui->shop_button = button; ui->shop_button_until = ui->ticks+10;
    /* LoadCoderScreen4DDDB..4DE18 encodes A..D,1..4 into four bytes. */
    if (button <= 8 && length < 4)
        ui->shop_code[length] = (char)(button+(button <= 4 ? 64 : 44));
    else if (button == 9) {
        unsigned event = (unsigned)ui->shop_arg;
        if (event > e->event_count)
            return cm_fail("coder",event,"coder target exceeds scene events");
        ui->shop_success = memcmp(ui->shop_code,e->coderStr+ui->shop_screen*8,5) == 0;
        if (ui->shop_success) {
            events_event *v = &e->events[event];
            v->state = 0;
            v->value[1] ^= 1;
            v->value[11] = -1; /* CM_HI.EXE4DEB6..4DEEE. */
        }
    } else if (button == 10) {
        memset(ui->shop_code,0,sizeof ui->shop_code); ui->shop_success = 0;
    } else if (button == 11) ui->shop_kind = 0;
    return 1;
}

int ui_shop_step(cm_ui *ui, events_state *e, world *w,
                   const cm_input *in, uint32_t ticks)
{
    int buy = in->pressed[48] || in->pressed[57];
    int sell = in->pressed[15] || in->pressed[31];
    int trade = in->pressed[28];
    ui->ticks = ticks;
    if (e->shop_requested.kind) {
        if (ui->shop_kind)
            return cm_fail("shop", 0, "overlapping shop requests");
        if (e->shop_requested.kind < 1 || e->shop_requested.kind > 6)
            return cm_fail("shop", (uint32_t)e->shop_requested.kind, "unsupported shop kind");
        ui->shop_kind = e->shop_requested.kind;
        ui->shop_switch = e->shop_requested.switch_index;
        ui->shop_screen = e->shop_requested.screen;
        ui->shop_arg = e->shop_requested.arg;
        if (ui->shop_kind == 2 || ui->shop_kind == 4) {
            e->shop_requested.kind=0;
            return ui_gambling_open(ui,e,w);
        }
        if (ui->shop_kind == 3) {
            if (ui->shop_screen < 0 || ui->shop_screen >= 3 ||
                ui->shop_arg < 0 || (unsigned)ui->shop_arg > e->event_count)
                return cm_fail("coder",(uint32_t)ui->shop_screen,"invalid coder slot/event");
            e->coderSlot = ui->shop_screen;
            e->coderEvent[ui->shop_screen] = ui->shop_arg;
            memset(ui->shop_code,0,sizeof ui->shop_code);
            ui->shop_button = 0; ui->shop_success = 0;
            e->shop_requested.kind = 0;
            return 1;
        }
        ui->shop_selected = 1; ui->shop_first = 0; ui->shop_selling = 0;
        if (shop_items(ui,e)) return -1;
        e->shop_requested.kind = 0;
        return 1;
    }
    if (!ui->shop_kind) return 0;
    if (ui->shop_kind == 2 || ui->shop_kind == 4)
        return ui_gambling_step(ui,e,w,in);
    if (in->pressed[1]) { ui->shop_kind = 0; return 1; }
    if (ui->shop_kind == 3) return coder_step(ui,e,in);
    if (ui->shop_count) {
        if (in->pressed[75]) ui->shop_selected =
            (ui->shop_selected+ui->shop_count-1)%ui->shop_count;
        if (in->pressed[77]) ui->shop_selected = (ui->shop_selected+1)%ui->shop_count;
    }
    if (in->mouse_pressed & 1u) {
        if (inside(in,188,158,40,30)) { ui->shop_kind = 0; return 1; }
        if (inside(in,72,158,50,30)) sell = 1;
        else if (inside(in,136,158,40,30)) buy = 1;
        if (ui->shop_count) {
            if (inside(in,6,145,24,26)) ui->shop_first =
                (ui->shop_first+ui->shop_count-1)%ui->shop_count;
            if (inside(in,284,145,24,26)) ui->shop_first =
                (ui->shop_first+1)%ui->shop_count;
            if (inside(in,0,60,106,84)) ui->shop_selected = ui->shop_first;
            if (inside(in,107,60,106,84)) ui->shop_selected =
                (ui->shop_first+1)%ui->shop_count;
            if (inside(in,214,60,106,84)) ui->shop_selected =
                (ui->shop_first+2)%ui->shop_count;
        }
    }
    /* ShopInputEvents49838..49889: action6 TAB/S, action7 SPACE/B.
     * The first press changes the mode; repeating it performs that trade. */
    if (buy || sell) {
        int selling = sell != 0;
        if (ui->shop_selling != selling) {
            ui->shop_selling = selling;
            ui->shop_first = 0; ui->shop_selected = 1;
            if (shop_items(ui,e)) return -1;
        } else trade = 1;
    }
    if (trade && shop_trade(ui,e,w)) return -1;
    return 1;
}

static void shop_bitmap(uint8_t *dst, int x, int y, const ui_bitmap *b)
{
    unsigned xx, yy;
    for (yy=0;yy<b->height;++yy) for (xx=0;xx<b->width;++xx) {
        uint8_t color = b->pixels[yy*b->width+xx];
        int dx=(x+(int)xx)*2,dy=(y+(int)yy)*2;
        if (dx < 0 || dy < 0 || dx+1 >= CM_WIDTH || dy+1 >= CM_HEIGHT) continue;
        gfx_store(dst+dy*CM_WIDTH+dx,color);
        gfx_store(dst+dy*CM_WIDTH+dx+1,color);
        gfx_store(dst+(dy+1)*CM_WIDTH+dx,color);
        gfx_store(dst+(dy+1)*CM_WIDTH+dx+1,color);
    }
}

static int shop_label(const cm_ui *ui, unsigned string, int32_t value,
                        char *dst, size_t capacity)
{
    const char *format = ui->strings[string], *mark = strchr(format,'%');
    char number[12];
    size_t prefix, suffix;
    int n;
    if (!mark || mark[1] != 'd' || strchr(mark+2,'%'))
        return cm_fail("STRINGS.MSG",string,"shop label requires one decimal field");
    n = snprintf(number,sizeof number,"%d",value);
    prefix = (size_t)(mark-format); suffix = strlen(mark+2);
    if (n < 0 || (size_t)n >= sizeof number || prefix >= capacity ||
        (size_t)n >= capacity-prefix || suffix >= capacity-prefix-(size_t)n)
        return cm_fail("STRINGS.MSG",string,"shop label exceeds display buffer");
    memcpy(dst,format,prefix);
    memcpy(dst+prefix,number,(size_t)n);
    memcpy(dst+prefix+(size_t)n,mark+2,suffix+1);
    return 0;
}

int ui_shop_draw(cm_ui *ui, const events_state *e, world *w, uint8_t *dst)
{
    unsigned x,y,i;
    char label[64];
    int width;
    if (!ui->shop_kind) return 0;
    if (ui->shop_kind == 2 || ui->shop_kind == 4)
        return ui_gambling_draw(ui,e,dst);
    for (y=0;y<CM_HEIGHT;++y) for (x=0;x<CM_WIDTH;++x)
        gfx_store(dst+y*CM_WIDTH+x,ui->shop_background[ui->shop_kind == 3 ? 2 : 0][y/2*320+x/2]);
    if (ui->shop_kind == 3) {
        char symbols[5] = {0};
        unsigned length = (unsigned)strlen(ui->shop_code);
        if (ui->shop_success) shop_bitmap(dst,92,192,&ui->shop_gfx[2][11]);
        if (ui->shop_button && ui->ticks <= ui->shop_button_until)
            shop_bitmap(dst,ui->coder_x[ui->shop_button],ui->coder_y[ui->shop_button],
                        &ui->shop_gfx[2][ui->shop_button-1]);
        for (i=0;i<length;++i)
            symbols[i] = (char)(ui->shop_code[i]-(ui->shop_code[i]>='A'?64:44));
        /* LoadCoderScreen4D989: font5 symbol cells are40 pixels. */
        return ui_text_mode(dst,&ui->fonts[5],404-(int)length*40,72,
                            symbols,117,1,0,1);
    }
    for (i=0;i<3 && i<ui->shop_count;++i) {
        unsigned at=(ui->shop_first+i)%ui->shop_count;
        const events_definition *d=&e->definitions[ui->shop_items[at]];
        if (ui_draw_shop_object(w,dst,104+(int)i*214,202,d->icon)) return -1;
        if (at == ui->shop_selected && ui->shop_gfx_count[0])
            shop_bitmap(dst,76+(int)i*107,60,&ui->shop_gfx[0][0]);
    }
    if (ui->shop_count) {
        int32_t price;
        const events_definition *d=&e->definitions[ui->shop_items[ui->shop_selected]];
        char name[21];
        memcpy(name,d->name,20); name[20]=0;
        if (gfx_text_width(&ui->fonts[3],name,&width) ||
            ui_text_mode(dst,&ui->fonts[3],320-width/2,52,name,117,0,0,0) ||
            shop_price(ui,e,ui->shop_selected,&price)) return -1;
        if (shop_label(ui,ui->shop_selling?187:186,price,label,sizeof label) ||
            gfx_text_width(&ui->fonts[3],label,&width) ||
            ui_text_mode(dst,&ui->fonts[3],608-width,76,label,232,0,0,0)) return -1;
    }
    if (shop_label(ui,189,e->cash,label,sizeof label) ||
        gfx_text_width(&ui->fonts[3],label,&width) ||
        ui_text_mode(dst,&ui->fonts[3],538-width,448,label,232,0,0,0)) return -1;
    return 0;
}
