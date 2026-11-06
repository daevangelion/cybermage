#include "gfx.h"

#include <string.h>

/* CM_HI.EXE Video::SetPalette: VGA DAC values become RGB8 by shifting two. */
int gfx_palette(uint8_t rgb[768], const uint8_t *data, uint32_t size,
                const char *path)
{
    unsigned i;
    if (size != 768)
        return cm_fail(path, 0, "palette must contain 256 RGB triples");
    for (i = 0; i < 768; ++i)
        if (data[i] > 63)
            return cm_fail(path, i, "palette component exceeds VGA DAC range");
    for (i = 0; i < 768; ++i)
        rgb[i] = (uint8_t)(data[i] << 2);
    return 0;
}

/* FONT_LOAD 0x66F00 copies the 14-byte disk header to descriptor+18.
 * TEXT_LEN 0x67589 and TEXT_CH 0x676D6 address fixed-stride glyphs.
 * Header+6 is glyph count: retail fonts have 128 or 10, not 256. */
int gfx_font(cm_font *f, const uint8_t *data, uint32_t size, const char *path)
{
    unsigned ch;
    uint16_t height, stride, count;
    if (size < 14)
        return cm_fail(path, 0, "truncated font header");
    height = cm_u16(data + 2);
    stride = cm_u16(data + 12);
    count = cm_u16(data + 6);
    if (!height || height > CM_HEIGHT || stride < 2 ||
        !count || count > 256 || (size - 14) / count < stride)
        return cm_fail(path, 2, "invalid font height, stride or glyph array");
    for (ch = 0; ch < count; ++ch) {
        uint32_t at = 14 + ch * stride;
        uint16_t width = cm_u16(data + at);
        if (width > CM_WIDTH || (uint32_t)width * height > (uint32_t)stride - 2)
            return cm_fail(path, at, "glyph pixels exceed stride");
    }
    f->data = data;
    f->height = height;
    f->stride = stride;
    f->count = count;
    f->path = path;
    f->spacing = cm_u16(data + 8);
    if (f->spacing >= 32768)
        f->spacing -= 65536;
    return 0;
}

int gfx_text_width(const cm_font *f, const char *text)
{
    int width = 0;
    while (*text) {
        const uint8_t *p;
        if ((unsigned char)*text >= f->count)
            return cm_fail(f->path, 6, "text character exceeds font glyph count");
        p = f->data + 14 + (unsigned char)*text++ * f->stride;
        width += cm_u16(p) + f->spacing;
    }
    return width;
}

static int text_draw(uint8_t *dst, const cm_font *f, int x, int y,
                     const char *text, int shadow)
{
    if (gfx_text_width(f, text) < 0)
        return -1;
    while (*text) {
        const uint8_t *g = f->data + 14 + (unsigned char)*text++ * f->stride;
        unsigned width = cm_u16(g), yy, xx;
        for (yy = 0; yy < f->height; ++yy) {
            int ty = y + (int)yy;
            if (ty < 0 || ty >= CM_HEIGHT)
                continue;
            for (xx = 0; xx < width; ++xx) {
                uint8_t c = g[2 + yy * width + xx];
                int tx = x + (int)xx;
                if (tx < 0 || tx >= CM_WIDTH || c == 255)
                    continue;
                if (c == 254) {
                    if (!shadow)
                        continue;
                    c = 1; /* title font TEXT_DEF 0x10EE8: shadow index 1 */
                }
                dst[ty * CM_WIDTH + tx] = c;
            }
        }
        x += (int)width + f->spacing;
    }
    return 0;
}

int gfx_text(uint8_t *dst, const cm_font *f, int x, int y, const char *text)
{
    return text_draw(dst, f, x, y, text, 0);
}

int gfx_menu_text(uint8_t *dst, const cm_font *f, int x, int y,
                  const char *text)
{
    return text_draw(dst, f, x, y, text, 1);
}

void gfx_blit(uint8_t *dst, int x, int y, const uint8_t *src,
              unsigned width, unsigned height)
{
    unsigned row, skip = 0, count = width;
    if (x >= CM_WIDTH || y >= CM_HEIGHT || width > CM_WIDTH || height > CM_HEIGHT)
        return;
    if (x < 0) {
        if (x <= -(int)width)
            return;
        skip = (unsigned)-x;
        count -= skip;
        x = 0;
    }
    if (count > (unsigned)(CM_WIDTH - x))
        count = (unsigned)(CM_WIDTH - x);
    for (row = 0; row < height; ++row) {
        int ty = y + (int)row;
        if (ty >= 0 && ty < CM_HEIGHT)
            memcpy(dst + ty * CM_WIDTH + x, src + row * width + skip, count);
    }
}

/* Xox::ShadeBox 0x4E2BC uses inclusive rectangle endpoints. */
void gfx_shade(uint8_t *dst, int x0, int y0, int x1, int y1,
               const uint8_t table[256])
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= CM_WIDTH) x1 = CM_WIDTH - 1;
    if (y1 >= CM_HEIGHT) y1 = CM_HEIGHT - 1;
    for (y = y0; y <= y1; ++y)
        for (x = x0; x <= x1; ++x)
            dst[y * CM_WIDTH + x] = table[dst[y * CM_WIDTH + x]];
}
