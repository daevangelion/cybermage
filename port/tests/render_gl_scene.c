#include "gfx.h"
#include "render_gl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_W 1920u
#define TEST_H 1080u
static world map;
static rsc hipak;
static uint8_t rgba[TEST_W*TEST_H*4],ppm[TEST_W*TEST_H*3+64];
static uint8_t software[CM_WIDTH*CM_HEIGHT],palette[768];

int main(int argc,char **argv)
{
    const uint8_t *pal;
    uint32_t size;
    unsigned width,height,x,y;
    world_camera camera={819328,1753216,166911,0,0,49152};
    char path[1024];int n,status=1,header;
    if (argc!=7) {platform_report("usage: render_gl_scene ASSETS OUTPUT_DIR WIDTH HEIGHT ANGLE PITCH");return 2;}
    width=(unsigned)strtoul(argv[3],NULL,10);height=(unsigned)strtoul(argv[4],NULL,10);
    camera.angle=(uint16_t)strtoul(argv[5],NULL,0);camera.pitch=(int32_t)strtol(argv[6],NULL,0);
    if (!width || !height || width>TEST_W || height>TEST_H) return 2;
    n=snprintf(path,sizeof path,"%s/HIPAK.RSC",argv[1]);if (n<0 || (size_t)n>=sizeof path) return 2;
    if (platform_init(1) || rsc_open(&hipak,path) || rsc_entry(&hipak,31,&pal,&size) ||
        gfx_palette(palette,pal,size,path) || world_load_scene(&map,argv[1],1) ||
        world_finalize_scene(&map) || render_load(&map,&hipak) ||
        render_gl_open(1,width,height) || render_gl_load(&map,render_get_state(),palette) ||
        render_gl_draw(&map,&camera) || render_gl_present(NULL) ||
        render_gl_read_rgba(rgba,sizeof rgba) || platform_directory(argv[2])) goto out;
    n=snprintf((char *)ppm,64,"P6\n%u %u\n255\n",width,height);
    if (n<0 || n>=64) goto out;
    header=n;
    for (y=0;y<height;++y) for (x=0;x<width;++x) {
        size_t source=((size_t)(height-1-y)*width+x)*4,dest=(size_t)n+((size_t)y*width+x)*3;
        memcpy(ppm+dest,rgba+source,3);
    }
    n=snprintf(path,sizeof path,"%s/gl-%ux%u.ppm",argv[2],width,height);
    if (n<0 || (size_t)n>=sizeof path) goto out;
    if (platform_file_write(path,ppm,(uint32_t)header+(uint32_t)(width*height*3))) goto out;
    if (width==CM_WIDTH && height==CM_HEIGHT) {
        if (render_set_view(0,0,CM_WIDTH,CM_HEIGHT) || render_draw(&map,&camera,software) ||
            platform_dump(argv[2],1,software,palette)) goto out;
    }
    printf("GL driver=%s size=%ux%u camera=%d,%d,%d,%u,%d\n",render_gl_driver(),width,height,
           camera.u,camera.v,camera.z,(unsigned)camera.angle,camera.pitch);
    status=0;
out:
    render_gl_close();render_unload();world_close(&map);rsc_close(&hipak);platform_shutdown();return status;
}
