#include "ui.h"
#include "events.h"

#include <stdio.h>
#include <string.h>

int ui_hud_from_world(ui_hud *hud, const world *w,
                      const events_state *e, unsigned phase, int hand_mode)
{
    unsigned i;
    uint32_t hp;
    if (!w->person_count || w->player_person >= w->person_count || phase > 7)
        return cm_fail("HUD", w->player_person, "invalid player or monitor phase");
    hp = (uint32_t)w->persons[0].unk_0038;
    memset(hud, 0, sizeof *hud);
    hud->hp = (int16_t)(hp & 65535u);
    hud->max_hp = (int16_t)(hp >> 16);
    hud->energy = e->energy;
    hud->max_energy = e->max_energy;
    hud->cash = e->cash;
    hud->hp_count = phase;
    hud->status = hand_mode;
    for (i = 0; i < 6; ++i) {
        hud->armor[i] = e->armor[i];
        hud->max_armor[i] = e->max_armor[i];
    }
    if (hand_mode == 1 || hand_mode == 2) {
        if (e->weapon >= 0 && e->weapon < 7) hud->ammo = e->ammo[e->weapon];
        else if (e->hh_weapon >= 0 && e->hh_weapon < EVENTS_ITEM_MAX)
            hud->ammo = e->items[e->hh_weapon].quantity;
        hud->weapon = hand_mode == 1;
    }
    return 0;
}

/* CM_HI.EXE DrawMonitor0x2A37C: index zero is transparent. */
void ui_draw_monitor(uint8_t *dst, int x, int y, const ui_bitmap *b)
{
    unsigned row, col;
    for (row = 0; row < b->height; ++row) {
        int dy = y + (int)row;
        if (dy < 0 || dy >= CM_HEIGHT) continue;
        for (col = 0; col < b->width; ++col) {
            int dx = x + (int)col;
            uint8_t color = b->pixels[row * b->width + col];
            if (dx >= 0 && dx < CM_WIDTH && color)
                gfx_store(dst+dy * CM_WIDTH + dx,color);
        }
    }
}

static int number(const cm_ui *ui, uint8_t *dst, int x, int y,
                   int value, unsigned digits, int encoding, uint8_t color)
{
    char text[16];
    const cm_font *f = &ui->fonts[6];
    unsigned i;
    int n;
    if (value < 0 || value > (digits == 2 ? 99 : 999))
        return cm_fail("HUD", 0, "monitor value outside three-digit range");
    n = snprintf(text, sizeof text, "%0*d", (int)digits, value);
    if (n != (int)digits) return cm_fail("HUD", 0, "monitor number formatting failed");
    for (i = 0; i < digits; ++i) {
        const uint8_t *g;
        unsigned row, col, width, ch = (unsigned)((int)(unsigned char)text[i] + encoding);
        if (ch >= f->count)
            return cm_fail(f->path, ch, "monitor digit exceeds font");
        g = f->data + 14 + ch * f->stride;
        width = cm_u16(g);
        for (row = 0; row < f->height; ++row)
            for (col = 0; col < width; ++col) {
                uint8_t c = g[2 + row * width + col];
                int dx = x + (int)col, dy = y + (int)row;
                if (dx < 0 || dx >= CM_WIDTH || dy < 0 || dy >= CM_HEIGHT || c == 255) continue;
                gfx_store(dst+dy * CM_WIDTH + dx,c == 254 ? 0 :
                    cm_u16(f->data + 4) ? c : color);
            }
        x += (int)width + f->spacing;
    }
    return 0;
}

/* StatusMonitor0x15F08/0x16088: 26*value/max, phase-1, 24-pixel lead;
 * clipped to124 pixels and copied as two opaque scanlines from HIPAK20. */
static int bar(const cm_ui *ui, uint8_t *dst, unsigned bitmap,
                int y, int value, int maximum, unsigned phase)
{
    const ui_bitmap *b = &ui->console[bitmap];
    int offset, skip = 0, count = b->width;
    unsigned row;
    if (maximum <= 0 || value < 0 || phase > 5)
        return cm_fail("HUD", bitmap, "invalid monitor bar state");
    if (value > maximum) value = maximum;
    offset = ((int)phase - 1) * (26 * value / maximum) - count + 24;
    if (offset < 0) { skip = -offset; count -= skip; offset = 0; }
    else if (offset + count > 124) count = 124 - offset;
    if (count <= 0) return 0;
    if (b->height != 2 || skip + count > b->width)
        return cm_fail("HIPAK.RSC", bitmap, "invalid monitor waveform dimensions");
    for (row = 0; row < 2; ++row) {
        memcpy(dst + (y + (int)row) * CM_WIDTH + 52 + offset,
               b->pixels + row * b->width + skip, (size_t)count);
        gfx_cover(dst + (y + (int)row) * CM_WIDTH + 52 + offset,(unsigned)count);
    }
    return 0;
}

int ui_render_bells_and_whistles(cm_ui *ui, uint8_t *dst,
                                const ui_hud *hud, const cm_draw_clock *clock)
{
    /* Constructor0x1026A–0x10333: monitors(10,436),(424,436). */
    unsigned phase = hud->hp_count;
    int y = ui->vdu == 2 ? 10 : 436;
    if (!ui->vdu || hud->status == 7) return 0;
    ui_draw_monitor(dst, 10, y, &ui->console[0]);
    ui_draw_monitor(dst, 424, y, &ui->console[1]);
    if (phase == 0)
        gfx_blit(dst, 26, y+4, ui->console[4].pixels,
                 ui->console[4].width, ui->console[4].height);
    if (phase <= 2)
        gfx_blit(dst, 42, y+6, ui->console[6 + phase].pixels,
                 ui->console[6 + phase].width, ui->console[6 + phase].height);
    if (phase > 0 && phase < 6 &&
        (bar(ui, dst, 10, y+10, hud->hp, hud->max_hp, phase) ||
         bar(ui, dst, 11, y+22, hud->energy, hud->max_energy, phase))) return -1;
    if (number(ui, dst, 176, y+4, hud->hp, 3, 0, 117) ||
        number(ui, dst, 176, y+20, hud->energy, 3, 49, 137)) return -1;
    /* StatusMonitor0x16227–0x16376; tables hp_X/hp_Y0x95BC0, armorColor0x962AC. */
    {
        static const int hp_x[6] = {64,42,20,172,150,128};
        static const int hp_y[6] = {20,14,8,8,14,20};
        static const uint8_t armor_color[4] = {1,117,232,174};
        const ui_bitmap *b = &ui->console[12];
        uint8_t colors[6];
        unsigned row, col;
        for (col = 0; col < 6; ++col) {
            unsigned level = 0;
            if (hud->armor[col] > 0) {
                if (hud->max_armor[col] <= 0)
                    return cm_fail("HUD", col, "armor maximum must be positive");
                level = (unsigned)(hud->armor[col] * 3 / hud->max_armor[col]) + 1;
                if (level > 3) level = 3;
                if (number(ui, dst, 424 + hp_x[col], y + hp_y[col],
                           hud->armor[col], 2, -28, 255)) return -1;
            }
            colors[col] = armor_color[level];
        }
        for (row = 0; row < b->height; ++row)
            for (col = 0; col < b->width; ++col) {
                uint8_t c = b->pixels[row * b->width + col];
                if (c >= 128 && c < 134) c = colors[c-128];
                gfx_store(dst+(y+4 + row) * CM_WIDTH + 518 + col,c);
            }
    }
    if (hud->weapon) {
        ui_draw_monitor(dst, 264, y+12, &ui->console[2]);
        gfx_blit(dst, 276, y+16, ui->console[9].pixels,
                 ui->console[9].width, ui->console[9].height);
        if (number(ui, dst, 294, y+18, hud->ammo, 3, 59, 174)) return -1;
    }
    return ui_status_advance(ui, clock);
}

int ui_status_advance(cm_ui *ui, const cm_draw_clock *clock)
{
    uint32_t ticks;
    if (!ui || !clock || !clock->read)
        return cm_fail("StatusMonitor", 0x16391, "missing draw clock");
    if (clock->read(clock->context, 0x16391, &ticks)) return -1;
    if (ticks > ui->blip_time) {
        ui->hp_count = (ui->hp_count + 1) % 8;
        ui->blip_time = ticks + 4;
    }
    return 0;
}
