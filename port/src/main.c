#include "gfx.h"
#include "platform.h"
#include "rsc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE_STRINGS_FIRST 140
#define TITLE_MENU_COUNT 6
#define PATH_BYTES 1024

static uint8_t pixels[CM_WIDTH * CM_HEIGHT];
static uint8_t palette[768];
static rsc hipak;
static cm_file strings;
static char resource_path[PATH_BYTES], strings_path[PATH_BYTES];
static const char *menu_text[TITLE_MENU_COUNT];

static int make_path(char *out, size_t size, const char *dir, const char *file)
{
    int n = snprintf(out, size, "%s/%s", dir, file);
    if (n < 0 || (size_t)n >= size)
        return cm_fail(dir, 0, "asset path too long");
    return 0;
}

/* Xox::LoadStrings 0x1FE00: sequential NUL strings; title group at 0x20115. */
static int title_strings(void)
{
    uint32_t at = 0;
    unsigned i;
    for (i = 0; i < TITLE_STRINGS_FIRST + TITLE_MENU_COUNT; ++i) {
        const uint8_t *end;
        if (at >= strings.size)
            return cm_fail(strings_path, at, "missing title string");
        end = memchr(strings.data + at, 0, strings.size - at);
        if (!end)
            return cm_fail(strings_path, at, "unterminated title string");
        if (i >= TITLE_STRINGS_FIRST) {
            if ((size_t)(end - strings.data - at) > 128)
                return cm_fail(strings_path, at, "title string exceeds menu width");
            menu_text[i - TITLE_STRINGS_FIRST] = (const char *)strings.data + at;
        }
        at = (uint32_t)(end - strings.data) + 1;
    }
    return 0;
}

static int title_draw(const uint8_t *title, const cm_font *font,
                      const uint8_t *shades, const uint8_t *bright,
                      unsigned selected)
{
    unsigned i;
    /* Xox::TitleMenu 0x555BA..0x55644, constants 0x95CB0..0x95D3C. */
    static const int center_x[3] = {122, 320, 518};
    memcpy(pixels, title, sizeof pixels);
    for (i = 0; i < TITLE_MENU_COUNT; ++i) {
        int col = (int)i % 3, row = (int)i / 3;
        int x0 = 32 + col * 198, y0 = 366 + row * 46;
        int text_y = 370 + row * 46;
        gfx_shade(pixels, x0, y0, x0 + 178, y0 + 32,
                  i == selected ? bright + 2 * 256 : shades + 22 * 256);
        if (gfx_menu_text(pixels, font,
                          center_x[col] - gfx_text_width(font, menu_text[i]) / 2,
                          text_y, menu_text[i])) return -1;
    }
    return 0;
}

static int number(const char *s, unsigned *n)
{
    unsigned long v;
    char *end;
    if (!*s || *s == '-') return -1;
    v = strtoul(s, &end, 10);
    if (*end || v > 1000000) return -1;
    *n = (unsigned)v;
    return 0;
}

int main(int argc, char **argv)
{
    const char *data = "orig/retail", *dump = NULL, *keys = NULL;
    const uint8_t *title, *font_data, *pal_data, *shades, *bright;
    uint32_t title_len, font_len, pal_len, shade_len, bright_len;
    cm_font font;
    cm_input input = {0};
    unsigned frames = 0, frame = 0, selected = 0;
    int headless = 0, status = 1, i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if ((!strcmp(argv[i], "--data") || !strcmp(argv[i], "--dump") ||
                  !strcmp(argv[i], "--keys") || !strcmp(argv[i], "--frames")) &&
                 i + 1 < argc) {
            const char *arg = argv[i++];
            if (!strcmp(arg, "--data")) data = argv[i];
            else if (!strcmp(arg, "--dump")) dump = argv[i];
            else if (!strcmp(arg, "--keys")) keys = argv[i];
            else if (number(argv[i], &frames) || !frames) {
                cm_fail("arguments", 0, "--frames must be 1..1000000");
                return 2;
            }
        } else {
            platform_report("usage: cybermage [--data DIR] [--headless] [--frames N] [--dump DIR] [--keys FILE]");
            return 2;
        }
    }
    if (headless && !frames) frames = 1;
    if (platform_init(headless)) goto out;
    if (make_path(resource_path, sizeof resource_path, data, "HIPAK.RSC") ||
        make_path(strings_path, sizeof strings_path, data, "STRINGS.MSG") ||
        rsc_open(&hipak, resource_path) ||
        platform_file_read(&strings, strings_path) || title_strings()) goto out;
    if (rsc_entry(&hipak, 0, &title, &title_len) ||
        rsc_entry(&hipak, 26, &font_data, &font_len) ||
        rsc_entry(&hipak, 31, &pal_data, &pal_len) ||
        rsc_entry(&hipak, 32, &shades, &shade_len) ||
        rsc_entry(&hipak, 33, &bright, &bright_len)) goto out;
    if (title_len != sizeof pixels || shade_len != 64 * 256 || bright_len != 10 * 256) {
        cm_fail(resource_path, 0, "invalid title or translation table size");
        goto out;
    }
    if (gfx_font(&font, font_data, font_len, resource_path) ||
        gfx_palette(palette, pal_data, pal_len, resource_path)) goto out;
    if (keys && platform_keys(keys)) goto out;
    while (!input.quit && (!frames || frame < frames)) {
        if (platform_poll(&input, frame)) goto out;
        if (input.pressed[75] && selected % 3) --selected;
        if (input.pressed[77] && selected % 3 < 2) ++selected;
        if (input.pressed[72] && selected >= 3) selected -= 3;
        if (input.pressed[80] && selected < 3) selected += 3;
        if (input.mouse_dx || input.mouse_dy) {
            if (input.mouse_x >= 32 && input.mouse_x <= 606 &&
                input.mouse_y >= 366 && input.mouse_y <= 444) {
                int col = (input.mouse_x - 32) / 198;
                if (col < 3) selected = (unsigned)col +
                    (input.mouse_y >= 412 ? 3u : 0u);
            }
        }
        if (input.pressed[1]) input.quit = 1;
        if (input.pressed[28]) {
            if (selected == 5) input.quit = 1;
            else {
                cm_fail("title", selected, "this port currently implements the title menu only");
                goto out;
            }
        }
        if (title_draw(title, &font, shades, bright, selected) ||
            platform_present(pixels, palette) ||
            (dump && platform_dump(dump, frame, pixels, palette))) goto out;
        platform_wait(frame++);
    }
    status = 0;
out:
    platform_file_release(&strings);
    rsc_close(&hipak);
    platform_shutdown();
    return status;
}
