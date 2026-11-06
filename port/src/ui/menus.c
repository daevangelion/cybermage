#include "ui.h"

#include <stdio.h>
#include <string.h>

#define UI_SCREEN_BYTES (CM_WIDTH * CM_HEIGHT)
#define UI_CONSOLE_HEADER 84u

static int screen_entry(rsc *r, unsigned index, const uint8_t **p)
{
    uint32_t size;
    if (rsc_entry(r, index, p, &size)) return -1;
    if (size != UI_SCREEN_BYTES)
        return cm_fail(r->path, index, "UI screen must be 640x480");
    return 0;
}

int ui_bitmap_bank(rsc *hipak, unsigned entry, ui_bitmap bitmaps[20],
                    unsigned *count)
{
    const uint8_t *p;
    uint32_t size, at = UI_CONSOLE_HEADER;
    unsigned i, n;
    if (rsc_entry(hipak, entry, &p, &size)) return -1;
    if (size < UI_CONSOLE_HEADER || (n = cm_u16(p)) > 20 ||
        cm_u16(p+2) != 20)
        return cm_fail(hipak->path, entry, "invalid UI bitmap bank header");
    for (i = 0; i < n; ++i) {
        ui_bitmap *b = &bitmaps[i];
        uint32_t bytes;
        b->width = cm_u16(p+4+i*2); b->height = cm_u16(p+44+i*2);
        bytes = (uint32_t)b->width*b->height;
        if (!b->width || !b->height || b->width > CM_WIDTH ||
            b->height > CM_HEIGHT || bytes > size-at)
            return cm_fail(hipak->path, at, "UI bitmap exceeds bank");
        b->pixels = p+at; at += bytes;
    }
    if (at != size)
        return cm_fail(hipak->path, at, "unexpected UI bitmap bank tail");
    *count = n;
    return 0;
}

int ui_init(cm_ui *ui, rsc *hipak, const cm_file *strings)
{
    static const unsigned font_entry[UI_FONT_COUNT] = {25,26,28,27,29,30,36};
    const uint8_t *p;
    uint32_t size, at = 0;
    unsigned i, count;
    ui_bitmap bank[20];
    memset(ui, 0, sizeof *ui);
    for (i = 0; i < UI_STRING_COUNT; ++i) {
        const uint8_t *end;
        if (at >= strings->size ||
            !(end = memchr(strings->data + at, 0, strings->size - at)))
            return cm_fail("STRINGS.MSG", at, "missing UI string terminator");
        ui->strings[i] = (const char *)strings->data + at;
        at = (uint32_t)(end - strings->data) + 1;
    }
    for (i = 0; i < UI_FONT_COUNT; ++i) {
        if (rsc_entry(hipak, font_entry[i], &p, &size) ||
            gfx_font(&ui->fonts[i], p, size, hipak->path)) return -1;
    }
    if (screen_entry(hipak, 0, &ui->title) ||
        screen_entry(hipak, 53, &ui->options_screen) ||
        screen_entry(hipak, 37, &ui->help_screen) ||
        screen_entry(hipak, 54, &ui->recorder_screen) ||
        rsc_entry(hipak, 32, &ui->shades, &size)) return -1;
    if (size != 64u * 256u)
        return cm_fail(hipak->path, 32, "invalid shadow tables");
    if (rsc_entry(hipak, 33, &ui->bright, &size)) return -1;
    if (size != 10u * 256u)
        return cm_fail(hipak->path, 33, "invalid bright tables");
    /* LoadConsoleGFX0x1FC9C: HIPAK20, 20 dimensions, then packed bitmaps. */
    if (ui_bitmap_bank(hipak, 20, bank, &count)) return -1;
    if (count != UI_CONSOLE_BITMAPS)
        return cm_fail(hipak->path, 20, "wrong console bitmap count");
    memcpy(ui->console, bank, sizeof ui->console);
    for (i = 0; i < 4; ++i) {
        static const unsigned backgrounds[4] = {4,5,6,50};
        static const unsigned widgets[4] = {19,21,22,23};
        if (rsc_entry(hipak, backgrounds[i], &ui->shop_background[i], &size) ||
            ui_bitmap_bank(hipak, widgets[i], ui->shop_gfx[i],
                           &ui->shop_gfx_count[i])) return -1;
        if (size != 320u*240u)
            return cm_fail(hipak->path, backgrounds[i], "invalid shop backdrop");
    }
    /* LoadSCONGFX0x21250: HIPAK35, 33 fixed 56x38 bitmaps. */
    if (rsc_entry(hipak, 35, &p, &size)) return -1;
    if (size < 4 || cm_u16(p) > 33 || cm_u16(p + 2) != 33 ||
        size != 4u + (uint32_t)cm_u16(p) * 56u * 38u)
        return cm_fail(hipak->path, 35, "invalid selector icon package");
    ui->scon_count = cm_u16(p);
    for (i = 0; i < ui->scon_count; ++i) {
        ui->scon[i].pixels = p + 4 + i * 56 * 38;
        ui->scon[i].width = 56; ui->scon[i].height = 38;
    }
    /* ReadConfigFile0x20560–0x205C4: defaults when CMDATA.CNF is absent. */
    ui->options[0] = 0;
    ui->options[3] = ui->options[5] = 75;
    ui->options[4] = 100;
    ui->options[8] = 1;
    ui->options[9] = 120;
    ui->restart_game = ui->restart_auto = -1;
    for (i = 0; i < UI_SAVE_SLOTS; ++i) ui->auto_ptr[i] = -1;
    ui_open(ui, UI_TITLE);
    return 0;
}

void ui_open(cm_ui *ui, ui_screen screen)
{
    ui->screen = screen;
    ui->selected = screen == UI_PAUSE ? 1u : 0u;
    ui->naming = 0;
    ui->blink = 1;
    ui->blink_timer = ui->ticks + 10;
}

static int option_y(unsigned index)
{
    int y = 146;
    unsigned i;
    for (i = 0; i < index; ++i) {
        y += 22;
        if (i == 2 || i == 5 || i == 7 || i == 8) y += 22;
    }
    return y;
}

void ui_speech_recorder(cm_ui *ui, const uint8_t speech_log[63])
{
    unsigned i;
    memcpy(ui->speechLog, speech_log, sizeof ui->speechLog);
    ui->recorder_count = 0;
    for (i = 0; i < 63; ++i)
        if (speech_log[i] && i != 21 && i != 22 && i != 42 &&
            i != 50 && i != 51 && i != 57)
            ui->recorder_map[ui->recorder_count++] = (uint8_t)i;
    ui_open(ui, UI_RECORDER);
}

ui_action ui_input(cm_ui *ui, const cm_input *in, uint32_t ticks)
{
    unsigned count = 0, columns = 1, i;
    int activate = in->pressed[28] || (in->mouse_pressed & 1u) ||
        ((in->mouse_buttons & 1u) && !(ui->mouse_buttons & 1u));
    ui->mouse_buttons = in->mouse_buttons;
    ui->ticks = ticks;
    if (ticks > ui->blink_timer) {
        ui->blink ^= 1;
        ui->blink_timer = ticks + 10;
    }
    if (in->quit) return UI_QUIT;
    if (ui->screen == UI_SAVE && ui->naming) {
        static const char keys[58] = {
            [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',
            [10]='9',[11]='0',[12]='-',[13]='=',[16]='q',[17]='w',[18]='e',
            [19]='r',[20]='t',[21]='y',[22]='u',[23]='i',[24]='o',[25]='p',
            [26]='[',[27]=']',[30]='a',[31]='s',[32]='d',[33]='f',[34]='g',
            [35]='h',[36]='j',[37]='k',[38]='l',[39]=';',[40]='\'',[41]='`',
            [43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',
            [50]='m',[51]=',',[52]='.',[53]='/',[57]=' '
        };
        char *name = ui->names[ui->selected];
        size_t length = strlen(name);
        if (in->pressed[1]) { ui->naming = 0; return UI_NONE; }
        if (in->pressed[28]) {
            ui->naming = 0; ui->slot = ui->selected; return UI_SAVE_GAME;
        }
        if (in->pressed[14] && length) name[--length] = 0;
        for (i = 2; i < sizeof keys; ++i)
            if (in->pressed[i] && keys[i] && length < UI_NAME_BYTES-1) {
                char c = keys[i];
                if ((in->held[42] || in->held[54]) && c >= 'a' && c <= 'z')
                    c = (char)(c-'a'+'A');
                name[length++] = c; name[length] = 0;
            }
        return UI_NONE;
    }
    if (ui->screen == UI_GAME) {
        if (in->pressed[1]) ui_open(ui, UI_PAUSE);
        else if (in->pressed[59]) ui_open(ui, UI_HELP);
        else if (in->pressed[63] || in->pressed[65]) {
            ui->return_screen = UI_GAME;
            ui_open(ui, in->pressed[63] ? UI_SAVE : UI_LOAD);
        }
        return UI_NONE;
    }
    if (in->pressed[1]) {
        if (ui->screen == UI_TITLE) return UI_QUIT;
        if (ui->screen == UI_OPTIONS) ui_open(ui, ui->return_screen);
        else if (ui->screen == UI_SAVE || ui->screen == UI_LOAD)
            ui_open(ui, ui->return_screen);
        else { ui_open(ui, UI_GAME); return UI_RESUME; }
        return UI_NONE;
    }
    switch (ui->screen) {
    case UI_TITLE: count = 6; columns = 3; break;
    case UI_PAUSE: count = 6; columns = 2; break;
    case UI_OPTIONS: case UI_SAVE: case UI_LOAD: count = 10; break;
    case UI_RECORDER: count = ui->recorder_count; columns = 2; break;
    case UI_HELP:
        if (activate) { ui_open(ui, UI_GAME); return UI_RESUME; }
        return UI_NONE;
    case UI_GAME: return UI_NONE;
    }
    if (in->pressed[72] && ui->selected >= columns) ui->selected -= columns;
    if (in->pressed[80] && ui->selected + columns < count) ui->selected += columns;
    if (ui->screen == UI_RECORDER) {
        if (in->pressed[73] && ui->selected >= 30) ui->selected -= 30;
        if (in->pressed[81] && ui->selected + 30 < count) ui->selected += 30;
    }
    if (columns > 1) {
        if (in->pressed[75] && ui->selected % columns) --ui->selected;
        if (in->pressed[77] && ui->selected % columns < columns - 1 &&
            ui->selected + 1 < count) ++ui->selected;
    }
    if (ui->screen == UI_OPTIONS) {
        int delta = (in->pressed[77] != 0) - (in->pressed[75] != 0);
        int *v = &ui->options[ui->selected];
        int minimum = ui->selected == 6 || ui->selected == 7 ? -320 : 0;
        int maximum = ui->selected == 0 || ui->selected == 8 ? 2 :
            ui->selected == 6 || ui->selected == 7 ? 320 :
            ui->selected == 9 ? 980 : 100;
        int step = ui->selected == 6 || ui->selected == 7 ? 32 :
            ui->selected == 9 ? 49 : ui->selected >= 3 && ui->selected <= 5 ? 5 : 1;
        if (delta && ui->selected != 1 && ui->selected != 2) {
            *v += delta * step;
            if (*v < minimum) *v = minimum;
            if (*v > maximum) *v = maximum;
        }
    }
    if (in->mouse_dx || in->mouse_dy || (in->mouse_pressed & 1u)) {
        for (i = 0; i < count; ++i) {
            int x0, x1, y0, y1;
            if (ui->screen == UI_TITLE) {
                x0 = 32 + (int)(i % 3) * 198; x1 = x0 + 178;
                y0 = 366 + (int)(i / 3) * 46; y1 = y0 + 32;
            } else if (ui->screen == UI_PAUSE) {
                x0 = i % 2 ? 342 : 100; x1 = x0 + 198;
                y0 = 168 + (int)(i / 2) * 86; y1 = y0 + 60;
            } else if (ui->screen == UI_RECORDER) {
                x0 = i % 2 ? 330 : 22; x1 = x0 + 286;
                y0 = 140 + (int)((i % 30) / 2) * 22; y1 = y0 + 20;
                if (i / 30 != ui->selected / 30) continue;
            } else {
                x0 = 46; x1 = 600;
                y0 = ui->screen == UI_OPTIONS ? option_y(i) : 164 + (int)i * 28;
                y1 = y0 + 20;
            }
            if (in->mouse_x >= x0 && in->mouse_x <= x1 &&
                in->mouse_y >= y0 && in->mouse_y <= y1) ui->selected = i;
        }
    }
    if (!activate) return UI_NONE;
    if (ui->screen == UI_TITLE) {
        static const ui_action actions[6] = {
            UI_NEW_GAME,UI_NONE,UI_NONE,UI_TRAINING,UI_CREDITS,UI_QUIT
        };
        if (ui->selected == 1 || ui->selected == 2) {
            ui->return_screen = UI_TITLE;
            ui_open(ui, ui->selected == 1 ? UI_LOAD : UI_OPTIONS);
            return UI_NONE;
        }
        return actions[ui->selected];
    }
    if (ui->screen == UI_PAUSE) {
        switch (ui->selected) {
        case 0: ui->return_screen = UI_PAUSE; ui_open(ui, UI_SAVE); break;
        case 1: ui_open(ui, UI_GAME); return UI_RESUME;
        case 2: ui->return_screen = UI_PAUSE; ui_open(ui, UI_LOAD); break;
        case 3: return UI_LOAD_GAME;
        case 4: return UI_QUIT;
        case 5: ui->return_screen = UI_PAUSE; ui_open(ui, UI_OPTIONS); break;
        }
    } else if (ui->screen == UI_SAVE || ui->screen == UI_LOAD) {
        ui->slot = ui->selected;
        if (ui->screen == UI_SAVE) { ui->naming = 1; return UI_NONE; }
        return UI_LOAD_GAME;
    } else if (ui->screen == UI_RECORDER && ui->recorder_count) {
        ui->slot = ui->recorder_map[ui->selected];
        return UI_SPEECH_REPLAY;
    }
    return UI_NONE;
}

/* TEXT_STR0x675D4: monochrome glyphs use foreground, FE shadow, FF outline. */
int ui_text_mode(uint8_t *dst, const cm_font *f, int x, int y,
                  const char *s, uint8_t fg, uint8_t outline,
                  uint8_t shadow, int mode)
{
    int color = cm_u16(f->data + 4) != 0;
    int width_check;
    if (gfx_text_width(f, s, &width_check)) return -1;
    while (*s) {
        const uint8_t *g = f->data + 14 + (unsigned char)*s++ * f->stride;
        unsigned width = cm_u16(g), yy, xx;
        for (yy = 0; yy < f->height; ++yy)
            for (xx = 0; xx < width; ++xx) {
                uint8_t c = g[2 + yy * width + xx];
                int dx = x + (int)xx, dy = y + (int)yy;
                if (dx < 0 || dx >= CM_WIDTH || dy < 0 || dy >= CM_HEIGHT) continue;
                if (c == 255) { if (!mode) continue; c = outline; }
                else if (c == 254) c = shadow;
                else if (!color) c = fg;
                dst[dy * CM_WIDTH + dx] = c;
            }
        x += (int)width + f->spacing;
    }
    return 0;
}

static int centered(cm_ui *ui, uint8_t *dst, unsigned font, int cx,
                    int y, const char *s)
{
    int width;
    if (gfx_text_width(&ui->fonts[font], s, &width)) return -1;
    return gfx_menu_text(dst, &ui->fonts[font], cx - width / 2, y, s);
}

int ui_draw(cm_ui *ui, uint8_t dst[CM_WIDTH * CM_HEIGHT])
{
    unsigned i;
    if (ui->screen == UI_GAME) return 0;
    if (ui->screen == UI_HELP) {
        memcpy(dst, ui->help_screen, UI_SCREEN_BYTES);
        return 0;
    }
    if (ui->screen == UI_RECORDER) {
        unsigned first = ui->selected / 30 * 30;
        memcpy(dst, ui->recorder_screen, UI_SCREEN_BYTES);
        for (i = first; i < first + 30 && i < ui->recorder_count; ++i) {
            unsigned row = (i-first)/2, col = (i-first)%2;
            int selected = i == ui->selected && ui->blink;
            if (ui_text_mode(dst, &ui->fonts[3], col ? 330 : 22, 140+(int)row*22,
                             ui->strings[77+ui->recorder_map[i]],
                             selected ? 255 : 125, 117, selected ? 84 : 7,
                             selected)) return -1;
        }
        if (!ui->recorder_count &&
            ui_text_mode(dst, &ui->fonts[3], 22, 140, ui->strings[156],
                         125, 0, 7, 0)) return -1;
        return 0;
    }
    if (ui->screen == UI_TITLE) {
        static const int title_op_x[3] = {122,320,518};
        memcpy(dst, ui->title, UI_SCREEN_BYTES);
        for (i = 0; i < 6; ++i) {
            int x = 32 + (int)(i % 3) * 198, y = 366 + (int)(i / 3) * 46;
            gfx_shade(dst, x, y, x + 178, y + 32,
                i == ui->selected ? ui->bright + 2 * 256 : ui->shades + 22 * 256);
            if (centered(ui, dst, 1, title_op_x[i % 3], y + 4, ui->strings[140+i])) return -1;
        }
    } else if (ui->screen == UI_PAUSE) {
        for (i = 0; i < 6; ++i) {
            int x = i % 2 ? 342 : 100, y = 168 + (int)(i / 2) * 86;
            gfx_shade(dst, x, y, x + 198, y + 60,
                i == ui->selected ? ui->bright + 2 * 256 : ui->shades + 22 * 256);
            if (centered(ui, dst, 1, i % 2 ? 444 : 198, y + 18, ui->strings[16+i])) return -1;
        }
    } else if (ui->screen == UI_SAVE || ui->screen == UI_LOAD) {
        gfx_shade(dst, 246, 112, 392, 156, ui->shades + 22 * 256);
        if (centered(ui, dst, 1, 320, 122, ui->strings[ui->screen == UI_SAVE ? 22 : 23])) return -1;
        for (i = 0; i < UI_SAVE_SLOTS; ++i) {
            char text[32];
            int y = 164 + (int)i * 28;
            int n = snprintf(text, sizeof text, "%u) %s%s", i, ui->names[i],
                             ui->naming && i == ui->selected && ui->blink ? "_" : "");
            if (n < 0 || (size_t)n >= sizeof text)
                return cm_fail("save names", i, "save slot label too long");
            gfx_shade(dst, 82, y, 558, y + 20,
                i == ui->selected ? ui->bright + 2 * 256 : ui->shades + 22 * 256);
            if (gfx_menu_text(dst, &ui->fonts[4], 92, y + 6, text)) return -1;
        }
    } else if (ui->screen == UI_OPTIONS) {
        memcpy(dst, ui->options_screen, UI_SCREEN_BYTES);
        for (i = 0; i < 10; ++i) {
            int y = option_y(i), selected = i == ui->selected && ui->blink;
            char number[16];
            if (ui_text_mode(dst, &ui->fonts[3], 46, y, ui->strings[24+i],
                selected ? 255 : 125, 117, selected ? 84 : 7, selected)) return -1;
            if (i == 0 || i == 8) {
                unsigned v;
                int x = i == 0 ? 366 : 384;
                for (v = 0; v < 3; ++v) {
                    const char *s = ui->strings[(i == 0 ? 34 : 37) + v];
                    int active = (int)v == ui->options[i];
                    int width;
                    if (ui_text_mode(dst, &ui->fonts[3], x, y, s, active ? 255 : 175,
                                  175, active ? 84 : 10, active)) return -1;
                    if (gfx_text_width(&ui->fonts[3], s, &width)) return -1;
                    x += width + 8;
                }
            } else if (i >= 3) {
                int x = i == 6 || i == 7 ? 468 + (ui->options[i] * 10 / 32) * 6 :
                    408 + ui->options[i] / (i == 9 ? 49 : 5) * 6;
                int row;
                for (row = 0; row < 14; ++row)
                    if (x >= 0 && x + 4 <= CM_WIDTH && y + row < CM_HEIGHT)
                        memset(dst + (y + row) * CM_WIDTH + x, 117, 4);
                /* OptSlider0x533AF: literal0x91C8C is "%3d". */
                if (snprintf(number, sizeof number, "%3d", ui->options[i]) < 0) return -1;
                if (ui_text_mode(dst, &ui->fonts[3], 588, y, number, 175, 0, 10, 0)) return -1;
            }
        }
    }
    return 0;
}
