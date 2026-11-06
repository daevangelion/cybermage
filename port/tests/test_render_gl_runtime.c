#include "render_gl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static world w;
static uint8_t wall[128*128],flat[128*128],masked[4],panel[4]={9,9,9,9};
static uint8_t packed[16]={8,0,0,0,12,0,0,0,2,0,7,7,2,0,7,7};
static uint8_t shades[64*256],fixed[10*256],fog[21*256],palette[768],sky[1024*128];
static uint8_t pixels[64*64];
static uint8_t canvas[CM_WIDTH*CM_HEIGHT];
static uint8_t ui_pixels[CM_WIDTH*CM_HEIGHT];
static cm_overlay overlay;
static unsigned frame;

static uint32_t hash(void)
{
    const uint8_t *p=(const uint8_t *)&w;
    size_t i;uint32_t value=2166136261u;
    for (i=0;i<sizeof w;++i) value=(value^p[i])*16777619u;
    return value;
}
static void draw(void)
{
    uint32_t before=hash();
    assert(render_gl_draw(&w,&w.camera)==0);
    assert(render_gl_readback(pixels,sizeof pixels,64)==0);
    assert(hash()==before);++frame;
}
static unsigned pixel(unsigned x,unsigned y) {return pixels[(63-y)*64+x];}
static void image(unsigned id,unsigned kind,int width,int height,uint8_t *data,uint32_t bytes,int pack)
{
    world_texture *t=w.textures+id;
    memset(t,0,sizeof *t);t->kind=kind;t->width=width;t->height=height;
    t->pixels.data=data;t->pixels.size=bytes;t->packed=(uint8_t)pack;t->revision=1;
    t->half_w=8192;t->base=32768;
}
static void camera_test(void)
{
    uint8_t expected[sizeof pixels];
    world_camera saved=w.camera;
    unsigned i;
    w.objects[1].active=0;
    for (i=0;i<sizeof wall;++i) wall[i]=(uint8_t)(i*31+i/128*17);
    ++w.textures[0].revision;
    w.camera.u=(saved.u&~255)+128;w.camera.v=(saved.v&~255)+128;
    draw();memcpy(expected,pixels,sizeof expected);
    w.camera.u-=128;w.camera.v+=127;
    draw();assert(memcmp(expected,pixels,sizeof expected)==0);
    w.camera=saved;w.objects[1].active=1;
    memset(wall,5,sizeof wall);++w.textures[0].revision;
}
static void snapshot_test(void)
{
    uint8_t map[256];
    unsigned x,y;
    memset(ui_pixels,17,sizeof ui_pixels);
    for (x=0;x<256;++x) map[x]=(uint8_t)(x^128);
    assert(gfx_overlay_begin(&overlay,ui_pixels)==0);
    for (y=0;y<80;++y) for (x=0;x<160;++x)
        gfx_store(ui_pixels+y*CM_WIDTH+x,(uint8_t)(x<80 ? 0:255));
    gfx_shade(ui_pixels,160,40,319,159,map);
    for (y=80;y<160;++y)
        gfx_copy(ui_pixels+y*CM_WIDTH+320,ui_pixels+y*CM_WIDTH+160,160);
    assert(gfx_overlay_end()==0 && render_gl_read_canvas(&overlay,canvas)==0);
    /*64x48 centred in64x64: each native UI pixel covers exactly10x10
     * logical pixels. Reference real world indices, not CPU placeholders. */
    for (y=0;y<CM_HEIGHT;++y) for (x=0;x<CM_WIDTH;++x) {
        unsigned nx=x/10,ny=y/10+8,gx=nx*10+5,gy=(ny-8)*10+5;
        unsigned i=gy*CM_WIDTH+gx,op=overlay.coverage[i],expected=pixel(nx,ny);
        if (op==1) expected=ui_pixels[i];
        else if (op>=2) {
            unsigned source=overlay.source[i];
            int sx=(int)nx+((int)(source%CM_WIDTH)-(int)gx)/10;
            int sy=(int)ny+((int)(source/CM_WIDTH)-(int)gy)/10;
            assert(sx>=0 && sx<64 && sy>=0 && sy<64);
            expected=overlay.maps[op-2][pixel((unsigned)sx,(unsigned)sy)];
        }
        assert(canvas[y*CM_WIDTH+x]==expected);
    }
    assert(canvas[20*CM_WIDTH+20]==0 && canvas[20*CM_WIDTH+100]==255);
    assert(canvas[100*CM_WIDTH+200]==73 && canvas[100*CM_WIDTH+400]==73);
}
int main(void)
{
    render_state *assets=render_get_state();
    world_object *o=w.objects+1;
    unsigned i,x,y;
    for (i=0;i<sizeof shades;++i) shades[i]=(uint8_t)i;
    for (i=0;i<256;++i) {shades[256+i]=55;shades[16*256+i]=(uint8_t)(i+1);}
    for (i=0;i<sizeof fixed;++i) fixed[i]=(uint8_t)i;
    memset(fixed,42,256);
    for (i=0;i<256;++i) fixed[9*256+i]=(uint8_t)(i^128);
    for (i=0;i<sizeof fog;++i) fog[i]=(uint8_t)(200+i/256);
    for (i=0;i<768;++i) palette[i]=(uint8_t)(i/3);
    memset(wall,5,sizeof wall);memset(flat,77,sizeof flat);memset(sky,33,sizeof sky);
    world_trig_init(&w.trig);w.camera.u=w.camera.v=5*65536+32768;w.camera.z=32768;
    w.cells[0][5*32+5].wall_sides=0x01010101;w.map_id_texture[1]=0;
    w.curve_base[0]=6;w.curve_shift[0]=14;w.curve_scale[0]=1;
    w.shade_override.bias=6;w.shade_override.shift=14;w.shade_override.scale=1;w.shade_override.revision=1;
    image(0,WORLD_TEX_PANEL,64,64,wall,sizeof wall,0);
    image(1,WORLD_TEX_PROP,1,1,packed,sizeof packed,1);
    image(2,WORLD_TEX_PROP,1,1,masked,sizeof masked,0);w.texture_count=3;
    o->active=1;o->flags=0x20080000;o->x=w.camera.u+16384;o->y=w.camera.v;
    o->z=o->height=32768;o->radius=8192;o->texture=1;w.object_count=2;
    assets->shade_maps=shades;assets->fixed_maps=fixed;assets->fog_maps=fog;
    assets->backdrop.data=sky;assets->backdrop.size=sizeof sky;
    assert(platform_init(1)==0 && render_sync_textures(&w)==0);
    assert(render_gl_open(1,64,64)==0 && render_gl_load(&w,assets,palette)==0);
    draw();assert(pixel(32,20)==7);
    w.light_type[0] = 99;
    w.light_param[0] = -1;
    w.light_type[31] = 99; /* No geometry refers to descriptor31. */
    draw();assert(pixel(32,20)==7);
    camera_test();
    o->flags|=0x20000;draw();
    for (y=16;y<32;++y) for (x=24;x<40;++x)
        assert(pixel(x,y)==(((frame+x+y+1)&1) ? 6u:5u));
    o->flags=(o->flags&~UINT32_C(0x20000))|0x2000;draw();
    for (y=16;y<32;++y) for (x=24;x<40;++x)
        assert(pixel(x,y)==(((frame+x+y+1)&1) ? 135u:7u));
    o->flags=0x20080000;packed[10]=packed[11]=packed[14]=packed[15]=0;++w.textures[1].revision;
    draw();assert(pixel(32,20)==0);
    o->flags=0x20000000;o->texture=2;draw();
    for (i=0;i<sizeof pixels;++i) assert(pixels[i]==5);
    image(3,WORLD_TEX_FXO,1,1,panel,sizeof panel,0);w.texture_count=4;
    o->flags=0x08000000;o->texture=3;o->radius=o->height=32768;
    for (i=0;i<4;++i) {o->corner_x[i]=o->x;o->corner_y[i]=o->y+((i==0||i==3) ? -32768:32768);}
    draw();assert(pixel(32,32)==9);
    o->y+=65536;for (i=0;i<4;++i) o->corner_y[i]+=65536;
    draw();assert(pixel(32,32)==5);
    w.fog_on=1;draw();assert(pixel(32,32)==203);
    w.fog_on=0;w.light_type[1]=2;w.cells[0][5*32+5].light=1;
    draw();assert(pixel(32,32)==42);
    w.light_type[2]=6;w.curve_base[2]=0;w.curve_shift[2]=14;w.curve_scale[2]=1;
    w.cells[0][5*32+5].light=2;draw();assert(pixel(32,32)==55);
    w.light_type[3]=0;w.light_param[3]=16;w.cells[0][5*32+5].light=3;
    draw();assert(pixel(32,32)==6);
    o->active=0;w.cells[0][5*32+5].wall_sides=0;w.cells[0][5*32+5].light=0;
    image(4,WORLD_TEX_FXO,64,64,flat,sizeof flat,0);w.texture_count=5;w.object_count=3;
    o=w.objects+2;o->active=1;o->texture=4;o->flags=0x02400000;
    o->x=w.camera.u+32768;o->y=w.camera.v;o->radius=o->height=32768;
    draw();assert(pixel(32,60)==77 && pixel(32,20)==0);
    o->flags=0x02800000;o->z=65536;draw();assert(pixel(32,3)==77 && pixel(32,50)==0);
    o->active=0;w.sky_type=1;w.sky_fill=99;
    draw();assert(pixel(32,10)==33 && pixel(32,50)==0);
    w.camera.pitch=-240;draw();assert(pixel(32,0)==99 && pixel(32,32)==33);
    w.camera.pitch=0;w.sky_type=4;draw();assert(pixel(32,40)==33 && pixel(32,60)==0);
    w.sky_type=2;w.sky_fog=201;w.fog_on=1;draw();
    for (i=0;i<sizeof pixels;++i) assert(pixels[i]==201);
    {
        const cm_gl_view inset={8,4,24,20,CM_WIDTH,CM_HEIGHT,1};
        assert(render_gl_clear(17)==0);
        assert(render_gl_draw_view(&w,&w.camera,&inset)==0);
        assert(render_gl_readback(pixels,sizeof pixels,64)==0);
        assert(pixel(12,12)==201 && pixel(0,0)==17 && pixel(12,40)==17);
        assert(render_gl_read_canvas(NULL,canvas)==0);
        assert(canvas[40*CM_WIDTH+160]==201 && canvas[400*CM_WIDTH+160]==17);
        snapshot_test();
    }
    printf("GL primitives passed: %s\n",render_gl_driver());
    render_gl_close();render_unload();platform_shutdown();return 0;
}
