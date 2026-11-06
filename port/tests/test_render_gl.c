#include "gfx.h"
#include <assert.h>
#include <string.h>

static cm_overlay overlay;
static uint8_t pixels[CM_WIDTH*CM_HEIGHT];

int main(void)
{
    uint8_t a[256],b[256];
    unsigned i,op;
    for (i=0;i<256;++i) {a[i]=(uint8_t)(i^128);b[i]=(uint8_t)(i+17);}
    memset(pixels,3,sizeof pixels);
    assert(gfx_overlay_begin(&overlay,pixels)==0);
    gfx_store(pixels,0);gfx_store(pixels+1,255);
    assert(overlay.coverage[0]==1 && overlay.coverage[1]==1);
    assert(pixels[0]==0 && pixels[1]==255 && overlay.coverage[2]==0);
    gfx_map_pixel(pixels+2,a);
    op=overlay.coverage[2];assert(op>=2);
    for (i=0;i<256;++i) assert(overlay.maps[op-2][i]==a[i]);
    gfx_map_pixel(pixels+2,b);
    op=overlay.coverage[2];
    for (i=0;i<256;++i) assert(overlay.maps[op-2][i]==b[a[i]]);
    gfx_map_pixel(pixels,b);
    assert(overlay.coverage[0]==1 && pixels[0]==17);
    gfx_cover(pixels+3,4);
    for (i=3;i<7;++i) assert(overlay.coverage[i]==1);
    assert(gfx_overlay_end()==0);
    gfx_store(pixels+7,255);
    assert(pixels[7]==255 && overlay.coverage[7]==0);
    return 0;
}
