#include "gfx.h"
#include "render_gl.h"
#include "ai.h"
#include "events.h"
#include "ui.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_W 1920u
#define TEST_H 1080u
static world map;
static rsc hipak;
static uint8_t rgba[TEST_W*TEST_H*4];
static uint8_t software[CM_WIDTH*CM_HEIGHT],palette[768];
static uint8_t indexed[TEST_W*TEST_H];
static cm_overlay overlay;
static ai_state actors;
static events_state events;
static cm_ui ui;
static cm_file strings;

static int fixed_draw_time(void *context, uint32_t site, uint32_t *ticks)
{
    if (site != 0x16391) return cm_fail("GL HUD fixture", site, "unexpected timer instruction");
    *ticks = *(const uint32_t *)context;
    return 0;
}
static const cm_draw_clock draw_clock = {&ui.ticks, fixed_draw_time};

static int integer(const char *text,int32_t *value)
{
    char *end;
    long long n;
    errno=0;n=strtoll(text,&end,0);
    if (errno || !*text || *end || n<INT32_MIN || n>INT32_MAX) return -1;
    *value=(int32_t)n;return 0;
}
static uint32_t world_hash(void)
{
    const uint8_t *p=(const uint8_t *)&map;
    size_t i;
    uint32_t value=2166136261u;
    for (i=0;i<sizeof map;++i) value=(value^p[i])*16777619u;
    return value;
}

static int hud_draw(void)
{
    ui_hud hud;
    ui_view view={0,0,CM_WIDTH,CM_HEIGHT};
    if (ui_hud_from_world(&hud,&map,&events,0,5) ||
        ui_draw_live_hand(&ui,&map,software,5,&events,&actors,view) ||
        ui_render_bells_and_whistles(&ui, software, &hud, &draw_clock)) return -1;
    return 0;
}

static int overlay_test(unsigned width,unsigned height,int tint_world)
{
    uint8_t map_lut[256];
    unsigned x,y,i;
    int narrow=(uint64_t)width*CM_HEIGHT<=(uint64_t)height*CM_WIDTH;
    int num=narrow ? (int)width:(int)height,den=narrow ? CM_WIDTH:CM_HEIGHT;
    if (render_gl_readback(indexed,sizeof indexed,width)) return -1;
    memset(software,17,sizeof software);
    for (i=0;i<256;++i) map_lut[i]=(uint8_t)(i^128);
    if (gfx_overlay_begin(&overlay,software)) return -1;
    gfx_store(software+10*640+10,0);gfx_store(software+10*640+11,255);
    for (y=100;y<120;++y) for (x=100;x<120;++x) gfx_map_pixel(software+y*640+x,map_lut);
    gfx_map_pixel(software+110*640+110,map_lut);
    gfx_copy(software+121*640+100,software+100*640+100,20);
    if (tint_world) gfx_shade_world(software,0,0,CM_WIDTH-1,CM_HEIGHT-1,map_lut);
    if (gfx_overlay_end() || render_gl_present(&overlay) || render_gl_read_rgba(rgba,sizeof rgba)) return -1;
    for (y=0;y<height;++y) for (x=0;x<width;++x) {
        uint8_t index=indexed[(height-1-y)*width+x];
        int nx=(2*(int)x+1)*den-((int)width*den-CM_WIDTH*num);
        int ny=(2*(int)y+1)*den-((int)height*den-CM_HEIGHT*num);
        if (nx>=0 && nx<CM_WIDTH*2*num && ny>=0 && ny<CM_HEIGHT*2*num) {
            unsigned qx=(unsigned)(nx/(2*num)),qy=(unsigned)(ny/(2*num)),op=overlay.coverage[qy*640+qx];
            if (op==1) index=software[qy*640+qx];
            else if (op>=2) {
                unsigned src=overlay.source[qy*640+qx],sx=src%640,sy=src/640;
                int px=((2*(int)x+1)*den+2*((int)sx-(int)qx)*num)/(2*den);
                int py=((2*((int)height-1-(int)y)+1)*den+2*((int)qy-(int)sy)*num)/(2*den);
                if (px<0) px=0;
                if (py<0) py=0;
                if ((unsigned)px>=width) px=(int)width-1;
                if ((unsigned)py>=height) py=(int)height-1;
                index=overlay.maps[op-2][indexed[(unsigned)py*width+(unsigned)px]];
            }
        } else if (tint_world && overlay.world_map>=2) index=overlay.maps[overlay.world_map-2][index];
        if (memcmp(rgba+((height-1-y)*width+x)*4,palette+index*3,3))
            return cm_fail("GL overlay test",y*width+x,"resolved pixel differs from exact indexed operators");
    }
    return 0;
}

int main(int argc,char **argv)
{
    const uint8_t *pal;
    uint32_t size;
    unsigned width,height;
    int32_t requested_w,requested_h,angle;
    uint32_t before;
    world_camera camera={819328,1753216,166911,0,0,49152};
    char path[1024];int n,status=1,hud=argc==8;
    if ((argc!=7 && argc!=8) || (hud && strcmp(argv[7],"--hud"))) {
        platform_report("usage: render_gl_scene ASSETS OUTPUT_DIR WIDTH HEIGHT ANGLE PITCH [--hud]");return 2;
    }
    if (integer(argv[3],&requested_w) || integer(argv[4],&requested_h) ||
        integer(argv[5],&angle) || integer(argv[6],&camera.pitch) ||
        requested_w<=0 || requested_h<=0 || requested_w>(int32_t)TEST_W ||
        requested_h>(int32_t)TEST_H || angle<0 || angle>65535) return 2;
    width=(unsigned)requested_w;height=(unsigned)requested_h;camera.angle=(uint16_t)angle;
    n=snprintf(path,sizeof path,"%s/HIPAK.RSC",argv[1]);if (n<0 || (size_t)n>=sizeof path) return 2;
    if (platform_init(1) || rsc_open(&hipak,path) || rsc_entry(&hipak,31,&pal,&size) ||
        gfx_palette(palette,pal,size,path) || world_load_scene(&map,argv[1],1)) goto out;
    if (hud && ai_init(&actors,&map,1)) goto out;
    if (world_finalize_scene(&map) || render_load(&map,&hipak)) goto out;
    if (hud) {
        n=snprintf(path,sizeof path,"%s/STRINGS.MSG",argv[1]);
        if (n<0 || (size_t)n>=sizeof path || platform_file_read(&strings,path) ||
            ui_init(&ui,&hipak,&strings) || ui_load_hand_tables(&ui,argv[1]) ||
            events_init(&events,&map)) goto out;
        ui_open(&ui,UI_GAME);
    }
    before=world_hash();
    if (render_gl_open(1,width,height) || render_gl_palette(palette) ||
        overlay_test(width,height,0) || render_gl_load(&map,render_get_state(),palette) ||
        render_gl_draw(&map,&camera) || overlay_test(width,height,1)) goto out;
    if (world_hash()!=before) {cm_fail("GL world",0,"renderer modified game state");goto out;}
    if (hud) {
        memset(software,0,sizeof software);
        if (gfx_overlay_begin(&overlay,software) || hud_draw() || gfx_overlay_end()) goto out;
    }
    if (render_gl_present(hud ? &overlay:NULL) ||
        platform_gl_dump(argv[2],0,width,height,render_gl_read_rgba)) goto out;
    if (width==CM_WIDTH && height==CM_HEIGHT) {
        memset(software,0,sizeof software);
        if (render_set_view(0,0,CM_WIDTH,CM_HEIGHT) || render_draw(&map,&camera,software) || (hud && hud_draw()) ||
            platform_dump(argv[2],1,software,palette)) goto out;
    }
    printf("GL driver=%s size=%ux%u camera=%d,%d,%d,%u,%d\n",render_gl_driver(),width,height,
           camera.u,camera.v,camera.z,(unsigned)camera.angle,camera.pitch);
    status=0;
out:
    render_gl_close();render_unload();world_close(&map);rsc_close(&hipak);platform_file_release(&strings);platform_shutdown();return status;
}
