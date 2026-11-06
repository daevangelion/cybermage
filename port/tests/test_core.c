#include "gfx.h"
#include "rsc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t screen[CM_WIDTH * CM_HEIGHT];

static void test_rsc(void)
{
    uint8_t data[35] = {3,0,0,0,1,0,0,0,32};
    const uint8_t *p;
    uint32_t n;
    rsc r;
    data[20] = 3;
    memcpy(data + 32, "abc", 3);
    assert(rsc_parse(&r, data, sizeof data, "fixture") == 0);
    assert(rsc_entry(&r, 0, &p, &n) == 0 && n == 3);
    assert(memcmp(p, "abc", 3) == 0);
    assert(rsc_entry(&r, 3, &p, &n) == -1);
    assert(rsc_parse(&r, data, 31, "fixture") == -1);
    data[8] = 31;
    assert(rsc_parse(&r, data, sizeof data, "fixture") == -1);
    data[8] = 32;
    data[20] = 4;
    assert(rsc_parse(&r, data, sizeof data, "fixture") == -1);
    data[20] = 3;
    data[4] = 4;
    assert(rsc_parse(&r, data, sizeof data, "fixture") == -1);
}

static void test_scene(void)
{
    uint8_t data[875] = {12,0,0,0,12};
    rsc r;
    data[8] = 0x68;
    data[9] = 3;
    data[440] = 3;
    assert(rsc_parse(&r, data, sizeof data, "SCENE.RSC") == 0);
    assert(r.capacity == 108);
    assert(rsc_parse(&r, data, sizeof data - 1, "SCENE.RSC") == -1);
}

static void test_gfx(void)
{
    uint8_t font[14 + 256 * 6] = {0};
    uint8_t pal[768] = {0};
    uint8_t rgb[768];
    uint8_t image[4] = {1,2,3,4};
    cm_font f;
    unsigned at = 14 + 'A' * 6;
    font[0] = 2; /* maximum width */
    font[2] = 2; /* height */
    font[6] = 128; /* glyph count, not always 256 */
    font[12] = 6; /* glyph stride */
    font[at] = 2;
    font[at+2] = 42;
    font[at+3] = 0xfe;
    font[at+4] = 0xff;
    font[at+5] = 43;
    assert(gfx_font(&f, font, 14 + 128 * 6, "font") == 0);
    assert(gfx_text_width(&f, "AA") == 4);
    assert(gfx_text_width(&f, "\200") == -1);
    memset(screen, 9, sizeof screen);
    gfx_text(screen, &f, 0, 0, "A");
    assert(screen[0] == 42 && screen[1] == 9);
    assert(screen[CM_WIDTH] == 9 && screen[CM_WIDTH+1] == 43);
    assert(gfx_menu_text(screen, &f, 0, 0, "A") == 0);
    assert(screen[1] == 1 && screen[CM_WIDTH] == 9);
    gfx_blit(screen, -1, -1, image, 2, 2);
    assert(screen[0] == 4);
    pal[0] = 63;
    assert(gfx_palette(rgb, pal, sizeof pal, "palette") == 0);
    assert(rgb[0] == 252);
    pal[1] = 64;
    assert(gfx_palette(rgb, pal, sizeof pal, "palette") == -1);
    assert(gfx_font(&f, font, 13, "font") == -1);
    font[at] = 3;
    assert(gfx_font(&f, font, sizeof font, "font") == -1);
}

int main(void)
{
    test_rsc();
    test_scene();
    test_gfx();
    puts("core: resource bounds, font bounds, palette and clipped blits passed");
    return 0;
}
