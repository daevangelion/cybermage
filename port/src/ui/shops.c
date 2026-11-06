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

int ui_shop_step(cm_ui *ui, events_state *e, world *w,
                   const cm_input *in, uint32_t ticks)
{
    int trade = in->pressed[28] || in->pressed[57];
    ui->ticks = ticks;
    if (e->shop_requested.kind) {
        if (ui->shop_kind)
            return cm_fail("shop", 0, "overlapping shop requests");
        if (e->shop_requested.kind != 1 && e->shop_requested.kind != 5 &&
            e->shop_requested.kind != 6)
            return cm_fail("shop", (uint32_t)e->shop_requested.kind, "unsupported shop kind");
        ui->shop_kind = e->shop_requested.kind;
        ui->shop_switch = e->shop_requested.switch_index;
        ui->shop_screen = e->shop_requested.screen;
        ui->shop_arg = e->shop_requested.arg;
        ui->shop_selected = 1; ui->shop_first = 0; ui->shop_selling = 0;
        if (shop_items(ui,e)) return -1;
        e->shop_requested.kind = 0;
        return 1;
    }
    if (!ui->shop_kind) return 0;
    if (in->pressed[1]) { ui->shop_kind = 0; return 1; }
    if (in->pressed[31]) {
        ui->shop_selling = 1; ui->shop_selected = 1; ui->shop_first = 0;
        if (shop_items(ui,e)) return -1;
    }
    if (in->pressed[48]) {
        ui->shop_selling = 0; ui->shop_selected = 1; ui->shop_first = 0;
        if (shop_items(ui,e)) return -1;
    }
    if (ui->shop_count) {
        if (in->pressed[75]) ui->shop_selected =
            (ui->shop_selected+ui->shop_count-1)%ui->shop_count;
        if (in->pressed[77]) ui->shop_selected = (ui->shop_selected+1)%ui->shop_count;
    }
    if (in->mouse_pressed & 1u) {
        if (inside(in,188,158,40,30)) { ui->shop_kind = 0; return 1; }
        if (inside(in,72,158,50,30)) {
            ui->shop_selling = 1; ui->shop_first = 0;
            if (shop_items(ui,e)) return -1;
        } else if (inside(in,136,158,40,30)) {
            if (ui->shop_selling) {
                ui->shop_selling = 0; ui->shop_first = 0;
                if (shop_items(ui,e)) return -1;
            } else trade = 1;
        }
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
        dst[dy*CM_WIDTH+dx]=dst[dy*CM_WIDTH+dx+1]=color;
        dst[(dy+1)*CM_WIDTH+dx]=dst[(dy+1)*CM_WIDTH+dx+1]=color;
    }
}

int ui_shop_draw(cm_ui *ui, const events_state *e, world *w, uint8_t *dst)
{
    unsigned x,y,i;
    char label[64];
    int width;
    if (!ui->shop_kind) return 0;
    for (y=0;y<CM_HEIGHT;++y) for (x=0;x<CM_WIDTH;++x)
        dst[y*CM_WIDTH+x]=ui->shop_background[0][y/2*320+x/2];
    for (i=0;i<3 && i<ui->shop_count;++i) {
        unsigned at=(ui->shop_first+i)%ui->shop_count;
        const events_definition *d=&e->definitions[ui->shop_items[at]];
        if (ui_draw_prop_icon(w,dst,48+(int)i*214,164,d->icon)) return -1;
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
        if (snprintf(label,sizeof label,"%s %d",ui->shop_selling?"SELL":"BUY",price)<0 ||
            gfx_text_width(&ui->fonts[3],label,&width) ||
            ui_text_mode(dst,&ui->fonts[3],608-width,76,label,232,0,0,0)) return -1;
    }
    if (snprintf(label,sizeof label,"%d",e->cash)<0 ||
        gfx_text_width(&ui->fonts[3],label,&width) ||
        ui_text_mode(dst,&ui->fonts[3],538-width,448,label,232,0,0,0)) return -1;
    return 0;
}
