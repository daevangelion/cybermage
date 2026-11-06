#include "ui.h"
#include "events.h"

#include <stdio.h>
#include <string.h>

#define SCON_WIDTH 56u
#define SCON_HEIGHT 38u
#define SCON_SCALE 2u

/* DrawSCONBitMap0x56A60: empty texels translate existing pixels; second
 * scanline copies the first, rather than translating its own background. */
int ui_draw_scon_bitmap(const cm_ui *ui, uint8_t *dst, int x, int y,
                        unsigned icon, unsigned shade)
{
    unsigned row, col;
    if (icon >= ui->scon_count || shade >= 64 || x < 0 || y < 0 ||
        x > CM_WIDTH-(int)(SCON_WIDTH*SCON_SCALE) ||
        y > CM_HEIGHT-(int)(SCON_HEIGHT*SCON_SCALE))
        return cm_fail("SCON", icon, "selector bitmap outside package or screen");
    for (row = 0; row < SCON_HEIGHT; ++row) {
        uint8_t *line = dst + (y+(int)row*2)*CM_WIDTH+x;
        for (col = 0; col < SCON_WIDTH; ++col) {
            uint8_t c = ui->scon[icon].pixels[row*SCON_WIDTH+col];
            if (c) {
                gfx_store(line+col*2,c);
                gfx_store(line+col*2+1,c);
            } else {
                gfx_map_pixel(line+col*2,ui->shades+shade*256);
                gfx_map_pixel(line+col*2+1,ui->shades+shade*256);
            }
        }
        gfx_copy(line+CM_WIDTH,line,SCON_WIDTH*SCON_SCALE);
    }
    return 0;
}

/* DrawSCONObject0x56B3C: fit56x38, centre, mirrored column sampling.
 * Decoder consumes the same signed run representation as the software view. */
static int prop_image(world *w, uint8_t *dst, int x, int y,
                        unsigned resource, int shop)
{
    const world_texture *t;
    unsigned sw, sh, dw, dh, col, row;
    if (world_prop_frame(w,resource,0,&t)) return -1;
    if (t->height <= 0 || t->height > 1024 || t->width <= 0 || t->width > 1024)
        return cm_fail("SCON object",resource,"invalid prop dimensions");
    sw = (unsigned)t->height*2; sh = (unsigned)t->width*2;
    dw = shop ? sw/2 : sw; dh = shop ? sh/2 : sh;
    if (!shop) {
        if (dw > SCON_WIDTH) { dh = dh*SCON_WIDTH/dw; dw = SCON_WIDTH; }
        if (dh > SCON_HEIGHT) { dw = dw*SCON_HEIGHT/dh; dh = SCON_HEIGHT; }
    }
    if (!dw || !dh) return cm_fail("SCON object",resource,"prop collapses at selector scale");
    x += (shop ? 0 : (int)SCON_WIDTH)-(int)dw;
    y += (shop ? 0 : (int)SCON_HEIGHT)-(int)dh;
    for (col = 0; col < dw*2; ++col) {
        unsigned sx = sw-1-col*sw/(dw*2);
        uint32_t at = 0, logical = 0;
        int run = 0;
        if (t->packed) {
            if (sw > t->pixels.size/4)
                return cm_fail("SCON object",resource,"truncated prop column table");
            at = cm_u32(t->pixels.data+sx*4);
            if (at < sw*4 || at > t->pixels.size)
                return cm_fail("SCON object",at,"prop column outside payload");
        } else if ((uint64_t)sw*sh > t->pixels.size)
            return cm_fail("SCON object",resource,"truncated raw prop bitmap");
        for (row = 0; row < dh*2; ++row) {
            unsigned sy = row*sh/(dh*2);
            uint8_t c;
            int dx = x+(int)col, dy = y+(int)row;
            if (t->packed) {
                while (sy >= logical+(unsigned)(run < 0 ? -run : run)) {
                    logical += (unsigned)(run < 0 ? -run : run);
                    if (run > 0) at += (unsigned)run;
                    if (at > t->pixels.size || t->pixels.size-at < 2)
                        return cm_fail("SCON object",at,"truncated prop run");
                    run = cm_u16(t->pixels.data+at); at += 2;
                    if (run >= 32768) run -= 65536;
                    if (!run || run == -32768 || (unsigned)(run < 0 ? -run : run) > sh-logical ||
                        (run > 0 && (unsigned)run > t->pixels.size-at))
                        return cm_fail("SCON object",at-2,"invalid prop column run");
                }
                if (run < 0) continue;
                c = t->pixels.data[at+sy-logical];
            } else {
                c = t->pixels.data[sy*sw+sx];
                if (!c) continue;
            }
            if (dx >= 0 && dx < CM_WIDTH && dy >= 0 && dy < CM_HEIGHT)
                gfx_store(dst+dy*CM_WIDTH+dx,c);
        }
    }
    return 0;
}

int ui_draw_prop_icon(world *w, uint8_t *dst, int x, int y, unsigned resource)
{
    return prop_image(w,dst,x,y,resource,0);
}

/* DrawShopObject2A734: original size, centred, mirrored columns. */
int ui_draw_shop_object(world *w, uint8_t *dst, int x, int y, unsigned resource)
{
    return prop_image(w,dst,x,y,resource,1);
}

static void outline(uint8_t *dst, int x, int y)
{
    int i;
    for (i = 0; i <= 113; ++i) {
        gfx_store(dst+y*CM_WIDTH+x+i,117);
        gfx_store(dst+(y+77)*CM_WIDTH+x+i,117);
    }
    for (i = 0; i <= 77; ++i) {
        gfx_store(dst+(y+i)*CM_WIDTH+x,117);
        gfx_store(dst+(y+i)*CM_WIDTH+x+113,117);
    }
}

int ui_draw_selector(const cm_ui *ui, world *w, const events_state *e, uint8_t *dst)
{
    unsigned i;
    if (e->selector_count > EVENTS_SELECTOR_MAX ||
        (e->selector_count && e->selector_index >= e->selector_count))
        return cm_fail("selector",e->selector_count,"invalid selector extent");
    for (i = 0; i < e->selector_count; ++i) {
        char name[15], shortcut[16], value[32];
        const char *label;
        int x = 14+(int)(i%5)*116, y = 128+(int)(i/5)*88;
        int icon, width, amount, order = e->selector_order[i];
        unsigned id = (unsigned)e->selector_items[i];
        if (e->selector_kind == 2) {
            if (id >= 16) return cm_fail("selector",id,"spell id exceeds original names/tables");
            icon = ui->spellScon[id]; amount = ui->spellPower[id]; label = ui->strings[id];
            if (snprintf(value,sizeof value,"%d MANG",amount) < 0) return -1;
        } else {
            const events_item *item;
            if (id >= EVENTS_ITEM_MAX) return cm_fail("selector",id,"item index exceeds inventory");
            item = &e->items[id]; icon = item->icon; label = item->name; amount = item->quantity;
            if (icon || item->kind == 5) {
                if (icon < 0 || ui_draw_scon_bitmap(ui,dst,x+1,y+1,(unsigned)icon,32)) return -1;
            } else {
                gfx_shade(dst,x+1,y+1,x+112,y+76,ui->shades+32*256);
                if (item->object < 0 || ui_draw_prop_icon(w,dst,x+1,y+1,(unsigned)item->object)) return -1;
            }
            if (snprintf(value,sizeof value,"%d",amount) < 0) return -1;
        }
        if (e->selector_kind == 2 &&
            (icon < 0 || ui_draw_scon_bitmap(ui,dst,x+1,y+1,(unsigned)icon,32))) return -1;
        if (i == e->selector_index) outline(dst,x,y);
        if (snprintf(shortcut,sizeof shortcut,order < 12 ? "%d" : "#%d",
                     order < 12 ? order+1 : order-11) < 0) return -1;
        if (ui_text_mode(dst,&ui->fonts[0],x+2,y+2,shortcut,117,0,1,0)) return -1;
        if (amount >= 0) {
            if (gfx_text_width(&ui->fonts[0],value,&width) ||
                ui_text_mode(dst,&ui->fonts[0],x+111-width,y+2,value,232,0,1,0)) return -1;
        }
        {
            size_t len = 0;
            while (len < sizeof name-1 && label[len]) { name[len] = label[len]; ++len; }
            name[len] = 0;
        }
        if (gfx_text_width(&ui->fonts[0],name,&width) ||
            ui_text_mode(dst,&ui->fonts[0],x+58-width/2,y+66,name,174,0,1,0)) return -1;
    }
    return 0;
}
