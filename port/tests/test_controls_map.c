#include "controls.h"
#include "world.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static uint8_t pixels[CM_WIDTH * CM_HEIGHT];

int main(void)
{
    cm_controls c;
    cm_input physical = {0}, out;
    controls_init(&c);
    memset(pixels, 7, sizeof pixels);
    assert(!controls_draw_map(&c, &w, pixels));
    assert(pixels[12 * CM_WIDTH + 432] == 7);
    physical.held[0x0f] = 1;
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(c.map_visible);
    w.object_count = 1;
    w.objects[0].x = 2 * 65536 + 32768;
    w.objects[0].y = 3 * 65536 + 32768;
    w.cells[0][3 * 32 + 2].floor = 1;
    w.cells[0][3 * 32 + 2].wall_sides = 0x00000101;
    w.trig.cos[0] = 65536;
    assert(!controls_draw_map(&c, &w, pixels));
    assert(pixels[(12 + 18) * CM_WIDTH + 432 + 12] == 125);
    assert(pixels[(12 + 18 + 3) * CM_WIDTH + 432 + 12 + 3] == 255);
    assert(pixels[0] == 7);
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(c.map_visible); /* Holding map does not toggle repeatedly. */
    physical.held[0x0f] = 0;
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    physical.held[0x0f] = 1;
    controls_frame(&c, CONTROLS_FOOT, &physical, &out);
    assert(!c.map_visible);
    puts("controls map: live wall/player geometry and edge-triggered toggle PASS");
    return 0;
}
