#include "gfx.h"
#include "../src/render_gl/internal.h"
#include <assert.h>
#include <string.h>

static cm_overlay overlay;
static uint8_t pixels[CM_WIDTH*CM_HEIGHT];
static world map;

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
    {
        const uint8_t raw[]={1,2,3,4};
        const uint8_t packed[]={8,0,0,0,12,0,0,0,2,0,0,2,254,255};
        uint8_t image[4],alpha[4];
        render_texture t={0};
        t.data=raw;t.size=sizeof raw;t.width=t.height=1;
        assert(glr_texture_decode(&t,0,image,alpha,sizeof image)==0);
        assert(image[0]==1 && image[1]==3 && image[2]==2 && image[3]==4);
        t.data=packed;t.size=sizeof packed;
        assert(glr_texture_decode(&t,1,image,alpha,sizeof image)==0);
        assert(image[0]==0 && alpha[0]==255 && image[2]==2 && alpha[2]==255);
        assert(alpha[1]==0 && alpha[3]==0);
        t.size=3;
        assert(glr_texture_decode(&t,1,image,alpha,sizeof image)!=0);
    }
    {
        glr_vertex vertices[4];
        glr_batch batches[GLR_TEXTURE_MAX];
        unsigned count;
        map.texture_count=1;
        map.cells[0][0].floor=1;
        assert(glr_build_tiles(&map,vertices,1,batches,&count)==0);
        assert(count==1 && batches[0].count==1);
        assert(vertices[0].z==0 && vertices[0].x==0 && vertices[0].y==0);
        assert(vertices[2].x==1 && vertices[2].y==1);
        assert(glr_build_tiles(&map,vertices,0,batches,&count)!=0);
    }
    return 0;
}
