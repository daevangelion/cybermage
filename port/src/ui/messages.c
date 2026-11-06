#include "ui.h"

#define TEXT_PAGE_LINES 4u
#define TEXT_MESSAGE_LINES_MAX 256u
#define TEXT_COLOR 232

static void hline(uint8_t *dst, int x0, int x1, int y)
{
    int x;
    if (y < 0 || y >= CM_HEIGHT) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= CM_WIDTH) x1 = CM_WIDTH-1;
    for (x = x0; x <= x1; ++x) gfx_store(dst+y * CM_WIDTH + x,TEXT_COLOR);
}

static void vline(uint8_t *dst, int x, int y0, int y1)
{
    int y;
    if (x < 0 || x >= CM_WIDTH) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= CM_HEIGHT) y1 = CM_HEIGHT-1;
    for (y = y0; y <= y1; ++y) gfx_store(dst+y * CM_WIDTH + x,TEXT_COLOR);
}

/* LoadGame0510A5..0510E7 restores these absolute counters, not a fresh page.
 * LoadTextMessage0514E2 reloads its strings without resetting the counters. */
int ui_text_restore(cm_ui *ui, const ui_save_state *save, uint32_t revision)
{
    if (save->textTrigger && (save->textLine > TEXT_MESSAGE_LINES_MAX ||
        save->textAdd > TEXT_MESSAGE_LINES_MAX || save->textEnd > 1))
        return cm_fail("SAVE/text", 0, "invalid saved text cursor");
    ui->text_revision = revision;
    ui->text_deadline = save->textCount;
    ui->text_line = save->textLine;
    ui->text_add = save->textAdd;
    ui->text_end = save->textEnd;
    return 0;
}

void ui_text_export(const cm_ui *ui, ui_save_state *save)
{
    save->textCount = ui->text_deadline;
    save->textLine = ui->text_line;
    save->textAdd = ui->text_add;
    save->textEnd = ui->text_end;
}

/* TextMessage0x17F88–0x1893B displays whole pages, not one character/tick.
 * Four lines/page; @ begins a new page. ^n/%n prefixes are control metadata.
 * Gameplay effects (#speaker, switches/video links) remain with Events/Audio.
 * Returns1 after the last page,0 while displayed,-1 on malformed content. */
int ui_text_message(cm_ui *ui, const world *w, uint8_t *dst,
                    const char *const *lines, unsigned count,
                    uint32_t revision, int object, ui_view view)
{
    const char *page[TEXT_PAGE_LINES];
    unsigned n = 0, i, skipped = 0;
    int max_width = 0, center, x, y, height, width, bottom = 0;
    int32_t distance = 0;
    uint16_t angle = 8192;
    if (count > TEXT_MESSAGE_LINES_MAX || view.width <= 0 || view.width > CM_WIDTH ||
        view.height <= 0 || view.height > CM_HEIGHT || object >= CM_OBJECT_MAX)
        return cm_fail("TEXT.MSG", count, "invalid message page or view");
    if (revision != ui->text_revision) {
        ui->text_revision = revision;
        ui->text_deadline = 0; ui->text_line = ui->text_add = 0;
        ui->text_end = 0;
    }
    /* 017FA6 JGE is signed and strict. Even textEnd waits for textCount.
     * Biasing both unsigned bit patterns preserves signed32 ordering. */
    if ((ui->ticks ^ UINT32_C(0x80000000)) >
        (ui->text_deadline ^ UINT32_C(0x80000000))) {
        ui->text_line += ui->text_add;
        ui->text_deadline = 0;
        if (ui->text_line >= count) ui->text_end = 1;
        if (ui->text_end) return 1;
    }
    if (ui->text_end) return 0;
    while (ui->text_line + skipped < count && n < TEXT_PAGE_LINES) {
        const char *s = lines[ui->text_line + skipped];
        int line_width;
        if (!s) return cm_fail("TEXT.MSG", ui->text_line + skipped, "null message line");
        if (*s == '@') {
            if (n) break;
            ++s;
        }
        ++skipped;
        if (*s == '#') continue;
        if (*s == '^' || *s == '%') {
            if (!s[1]) return cm_fail("TEXT.MSG", ui->text_line + skipped-1, "truncated message directive");
            s += 2;
        }
        if (gfx_text_width(&ui->fonts[2], s, &line_width)) return -1;
        if (line_width > CM_WIDTH-36)
            return cm_fail("TEXT.MSG", ui->text_line + skipped-1, "message line exceeds screen width");
        if (line_width > max_width) max_width = line_width;
        page[n++] = s;
    }
    ui->text_add = skipped;
    if (!n) return 0;
    if (object > 0) {
        const world_object *speaker = &w->objects[object];
        const world_object *player;
        uint16_t target;
        if (w->player_object >= CM_OBJECT_MAX)
            return cm_fail("TEXT.MSG", w->player_object, "player object exceeds world");
        player = &w->objects[w->player_object];
        target = cm_atan64k(player->x, player->y, speaker->x, speaker->y);
        angle = (uint16_t)(player->heading-target+8192);
        distance = cm_distance(player->x, player->y, speaker->x, speaker->y);
        bottom = (int64_t)player->floor_z + player->z <= speaker->floor_z;
    }
    center = view.x + view.width * (int)angle / 16384;
    width = max_width + 20;
    height = (int)n * 28 + 6;
    x = center-max_width/2-10;
    if (x + width + 8 > CM_WIDTH) x = CM_WIDTH-width-8;
    if (x < 8) x = 8;
    y = distance / 4096;
    if (y < 2) y = 2;
    y = view.y + y * view.width / CM_WIDTH;
    if (bottom) {
        y += view.height/3;
        if (y + height > 464) y = CM_HEIGHT-height-16;
    } else {
        if (y + height > 160) y = 160-height;
        if (y < 2) y = 2;
    }
    for (i = 0; i < n; ++i)
        if (ui_text_mode(dst, &ui->fonts[2], x+10, y+6+(int)i*28,
                         page[i], TEXT_COLOR, 0, 1, 0)) return -1;
    {
        static const unsigned corners[4] = {24,25,26,27};
        int xx[4] = {x,x+width-6,x+width-6,x};
        int yy[4] = {y,y,y+height-6,y+height-6};
        for (i = 0; i < 4; ++i) {
            char glyph[2] = {(char)corners[i],0};
            if (ui_text_mode(dst, &ui->fonts[2], xx[i], yy[i], glyph,
                             TEXT_COLOR, 0, 1, 0)) return -1;
        }
    }
    hline(dst, x+6, x+width-6, y);
    hline(dst, x+6, x+width-6, y+height);
    vline(dst, x, y+6, y+height-6);
    vline(dst, x+width, y+6, y+height-6);
    if (!ui->text_deadline) {
        if (ui->options[9] < 0 || ui->options[9] > 980)
            return cm_fail("TEXT.MSG", 0, "message delay outside original slider range");
        ui->text_deadline = ui->ticks + (skipped+1u)*(unsigned)ui->options[9];
    }
    return 0;
}
