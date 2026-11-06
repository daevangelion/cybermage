#include "gfx.h"

#include <string.h>

#define TEXT_LIMIT (32 * CM_WIDTH)

static cm_overlay *active_overlay;
static uint8_t identity_map[256];
static int identity_ready;

int gfx_overlay_begin(cm_overlay *overlay,uint8_t *pixels)
{
    unsigned i;
    if (!overlay || !pixels || active_overlay)
        return cm_fail("UI overlay",0,"invalid or nested overlay capture");
    overlay->pixels=pixels;overlay->map_count=0;overlay->world_map=0;overlay->error=0;
    overlay->last_source=NULL;
    if (!identity_ready) {
        for (i=0;i<256;++i) identity_map[i]=(uint8_t)i;
        identity_ready=1;
    }
    memset(overlay->coverage,0,sizeof overlay->coverage);
    active_overlay=overlay;
    return 0;
}

int gfx_overlay_end(void)
{
    int error;
    if (!active_overlay) return cm_fail("UI overlay",0,"overlay capture not active");
    error=active_overlay->error;active_overlay=NULL;
    return error;
}

static uint8_t *coverage(const uint8_t *dst)
{
    uintptr_t offset;
    if (!active_overlay) return NULL;
    offset=(uintptr_t)dst-(uintptr_t)active_overlay->pixels;
    return offset<sizeof active_overlay->coverage ? active_overlay->coverage+offset:NULL;
}

void gfx_store(uint8_t *dst,uint8_t color)
{
    uint8_t *op=coverage(dst);
    *dst=color;
    if (op) *op=1;
}

void gfx_cover(uint8_t *dst,unsigned count)
{
    uint8_t *op=coverage(dst);
    if (op) {
        size_t offset=(size_t)(op-active_overlay->coverage);
        if (count>sizeof active_overlay->coverage-offset) {
            active_overlay->error=cm_fail("UI overlay",(uint32_t)offset,"coverage span outside canvas");
            return;
        }
        memset(op,1,count);
    }
}

static int overlay_map(const uint8_t table[256],unsigned previous)
{
    cm_overlay *o=active_overlay;
    unsigned row,i;
    if (o->last_source==table && o->last_previous==previous) return o->last_row;
    for (row=0;row<o->map_count;++row)
        if (o->map_source[row]==table && o->previous[row]==previous) break;
    if (row==o->map_count) {
        if (row==CM_OVERLAY_MAPS) {
            o->error=cm_fail("UI overlay",row,"composed colormap bank full");
            return -1;
        }
        for (i=0;i<256;++i)
            o->maps[row][i]=table[previous==UINT16_MAX ? i:o->maps[previous][i]];
        o->map_source[row]=table;o->previous[row]=(uint16_t)previous;++o->map_count;
    }
    o->last_source=table;o->last_previous=(uint16_t)previous;o->last_row=(uint16_t)row;
    return (int)row;
}

void gfx_world_map(const uint8_t table[256])
{
    if (active_overlay) {
        unsigned previous=active_overlay->world_map ? active_overlay->world_map-2u:UINT16_MAX;
        int row=overlay_map(table,previous);
        if (row>=0) active_overlay->world_map=(uint16_t)(row+2);
    }
}

void gfx_map_pixel(uint8_t *dst,const uint8_t table[256])
{
    uint8_t *op=coverage(dst);
    if (op && *op!=1) {
        if (!*op) active_overlay->source[op-active_overlay->coverage]=(uint32_t)(op-active_overlay->coverage);
        int row=overlay_map(table,*op ? (unsigned)*op-2:UINT16_MAX);
        if (row>=0) *op=(uint8_t)(row+2);
    }
    *dst=table[*dst];
}

void gfx_copy(uint8_t *dst,const uint8_t *src,unsigned count)
{
    uint8_t *to=coverage(dst),*from=coverage(src);
    if (to && count) {
        cm_overlay *o=active_overlay;
        size_t d=(size_t)(to-o->coverage),s=from ? (size_t)(from-o->coverage):0;
        unsigned k;
        int identity=-1;
        if (count>sizeof o->coverage-d || (from && count>sizeof o->coverage-s)) {
            o->error=cm_fail("UI overlay",(uint32_t)d,"copy span outside canvas");
            return;
        }
        if (!from) memset(to,1,count);
        else for (k=0;k<count;++k) {
            unsigned i=d>s ? count-1-k:k;
            uint8_t op=from[i];
            if (!op) {
                if (identity<0) identity=overlay_map(identity_map,UINT16_MAX);
                if (identity<0) return;
                to[i]=(uint8_t)(identity+2);o->source[d+i]=(uint32_t)(s+i);
            } else {
                to[i]=op;
                if (op>=2) o->source[d+i]=o->source[s+i];
            }
        }
    }
    memmove(dst,src,count);
}

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

int gfx_text_width(const cm_font *f, const char *text, int *width)
{
    int64_t sum = 0;
    unsigned count = 0;
    while (*text) {
        const uint8_t *p;
        if (++count > TEXT_LIMIT)
            return cm_fail(f->path, 0, "text exceeds character limit");
        if ((unsigned char)*text >= f->count)
            return cm_fail(f->path, 6, "text character exceeds font glyph count");
        p = f->data + 14 + (unsigned char)*text++ * f->stride;
        sum += cm_u16(p) + f->spacing;
        if (sum < -TEXT_LIMIT || sum > TEXT_LIMIT)
            return cm_fail(f->path, 8, "text width exceeds draw limit");
    }
    *width = (int)sum;
    return 0;
}

static int text_draw(uint8_t *dst, const cm_font *f, int x, int y,
                     const char *text, int shadow)
{
    int width;
    if (gfx_text_width(f, text, &width))
        return -1;
    if (x < -TEXT_LIMIT || x > TEXT_LIMIT ||
        y < -TEXT_LIMIT || y > TEXT_LIMIT)
        return cm_fail(f->path, 0, "text origin exceeds draw limit");
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
                gfx_store(dst+ty*CM_WIDTH+tx,c);
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
        if (ty >= 0 && ty < CM_HEIGHT) {
            memcpy(dst + ty * CM_WIDTH + x, src + row * width + skip, count);
            gfx_cover(dst+ty*CM_WIDTH+x,count);
        }
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
            gfx_map_pixel(dst+y*CM_WIDTH+x,table);
}

void gfx_shade_world(uint8_t *dst,int x0,int y0,int x1,int y1,const uint8_t table[256])
{
    cm_overlay *overlay=active_overlay;
    unsigned i;
    int captured=overlay && dst==overlay->pixels;
    if (captured) {
        gfx_world_map(table);
        for (i=0;i<CM_WIDTH*CM_HEIGHT;++i) {
            unsigned op=overlay->coverage[i];
            int row;
            if (op==1) continue;
            row=overlay_map(table,op ? op-2:UINT16_MAX);
            if (row<0) break;
            if (!op) overlay->source[i]=i;
            overlay->coverage[i]=(uint8_t)(row+2);
        }
        /* World transforms are already recorded. CPU opaque UI still needs
         * the original rectangle, after hands/before the HUD when flashing. */
        active_overlay=NULL;
    }
    gfx_shade(dst,x0,y0,x1,y1,table);
    if (captured) active_overlay=overlay;
}
