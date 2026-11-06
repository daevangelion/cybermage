#include "ai.h"
#include "gfx.h"
#include "render.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static world map;
static ai_state actors;
static rsc hipak;
static uint8_t pixels[CM_WIDTH*CM_HEIGHT],palette[768];

static int coordinate(const char *text,int32_t *value)
{
    char *end;
    long long n;
    errno=0;n=strtoll(text,&end,0);
    if (errno || !*text || *end || n<INT32_MIN || n>INT32_MAX) return -1;
    *value=(int32_t)n;return 0;
}

int main(int argc,char **argv)
{
    const uint8_t *pal;
    uint32_t size;
    int32_t angle;
    char path[CM_WORLD_PATH_MAX];
    world_camera camera={0};
    int status=1,n;
    if (argc!=8 || coordinate(argv[3],&camera.u) || coordinate(argv[4],&camera.v) ||
        coordinate(argv[5],&camera.z) || coordinate(argv[6],&angle) ||
        coordinate(argv[7],&camera.pitch) || angle<0 || angle>65535) {
        platform_report("usage: render_scene ASSET_DIR OUTPUT_DIR U V Z ANGLE PITCH");return 2;
    }
    camera.angle=(uint16_t)angle;
    n=snprintf(path,sizeof path,"%s/HIPAK.RSC",argv[1]);
    if (n<0 || (size_t)n>=sizeof path) return 2;
    if (platform_init(1) || rsc_open(&hipak,path) ||
        rsc_entry(&hipak,31,&pal,&size) || gfx_palette(palette,pal,size,path) ||
        world_load_scene(&map,argv[1],1) || ai_init(&actors,&map,1) ||
        world_finalize_scene(&map) || render_load(&map,&hipak) ||
        render_draw(&map,&camera,pixels) || platform_dump(argv[2],0,pixels,palette)) goto out;
    printf("render_scene camera=%" PRId32 ",%" PRId32 ",%" PRId32 ",%u,%" PRId32
           " view=120,90,400,300 cells=%u objects=%u textures=%u\n",
           camera.u,camera.v,camera.z,(unsigned)camera.angle,camera.pitch,
           render_get_state()->scene.cell_count,render_get_state()->scene.generated_count,map.texture_count);
    status=0;
out:
    render_unload();world_close(&map);rsc_close(&hipak);platform_shutdown();
    return status;
}
