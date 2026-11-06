#include "render.h"
#include "target.h"
#include "../src/render/internal.h"
#include "../src/vis/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static world map;
static uint8_t framebuffer[CM_RENDER_WIDTH*CM_RENDER_HEIGHT];
static uint8_t texture[128*128], shades[64*256], fixed[10*256], fog[21*256];
static uint8_t floor_texture[128*128],ceiling_texture[128*128];
static render_visibility_list visible;

static uint32_t world_hash(void)
{
    const unsigned char *p=(const unsigned char *)&map;
    size_t i;
    uint32_t hash=2166136261u;
    for (i=0;i<sizeof map;++i) hash=(hash^p[i])*16777619u;
    return hash;
}

static unsigned compare_targets(void)
{
    target_list targets;
    vis_view view = {120, 90, 400, 300};
    render_state *state = render_get_state();
    unsigned i, count = 0, texture_count = map.texture_count;
    uint32_t hash;
    assert(render_visibility(&map, &map.camera, &visible) == 0);
    /* The primitive fixture installs pixel/extents directly in render state.
     * Supply the same extents to the independent, pixel-free consumer. */
    for (i = 0; i < 16; ++i) {
        map.textures[i].half_w = state->textures[i].frame_half_w;
        map.textures[i].base = state->textures[i].frame_base;
        map.textures[i].depth = state->textures[i].frame_depth;
        map.textures[i].frame_height = state->textures[i].frame_height;
    }
    map.texture_count = 16;
    hash = world_hash();
    assert(target_query(&targets, &map, &map.camera, &view) == 0);
    assert(hash == world_hash());
    map.texture_count = texture_count;
    for (i = 0; i < visible.count; ++i) {
        const render_visible_prop *p = &visible.props[i];
        if (p->targetable && p->left <= 320 && p->right >= 320) {
            assert(count < targets.count && p->object == targets.objects[count]);
            ++count;
        }
    }
    assert(count == targets.count);
    return count;
}

int main(void)
{
    render_state s;
    render_state *state=render_get_state();
    unsigned x,y,i;
    uint32_t hash;
    {
        int before;
        map.objects[1].x=INT32_MAX;map.objects[1].y=0x50000;
        map.objects[1].radius=map.objects[1].height=64;
        map.objects[2].x=INT32_MAX;map.objects[2].y=0x50000;
        state->scene.object_kind[1]=1;state->scene.object_kind[2]=2;
        before=vis_before(&map,&state->scene,1,2);
        assert(before==0 || before==1);
        memset(map.objects,0,sizeof map.objects);
    }
    memset(&s, 0, sizeof s);
    s.shade_bias = 6;
    s.shade_shift = 14;
    s.shade_scale = 1;
    assert(render_shade_distance(&s, 0) == 0);
    assert(render_shade_distance(&s, 7 * 16384) == 1);
    assert(render_shade_distance(&s, 100 * 16384) == 63);
    assert(render_fog_distance(-65536) == 0);
    assert(render_fog_distance(0) == 2);
    assert(render_fog_distance(100 * 16384) == 16);
    assert(render_set_view(-1, 0, 400, 300) != 0);
    assert(render_set_view(120, 90, 400, 300) == 0);
    assert(render_get_state()->focal == 200);
    world_trig_init(&map.trig);
    map.camera.u=5*65536+32768;map.camera.v=5*65536+32768;
    map.camera.z=32768;map.camera.angle=0;map.top_level=0;
    map.map_id_texture[1]=1;
    for (i=0;i<CM_MAP_CELLS;++i) map.cells[0][i].wall_sides=0x01010101;
    map.curve_base[0]=6;map.curve_shift[0]=14;map.curve_scale[0]=1;
    memset(texture,7,sizeof texture);
    for (i=0;i<sizeof shades;++i) shades[i]=(uint8_t)i;
    for (i=0;i<sizeof fixed;++i) fixed[i]=(uint8_t)i;
    for (i=0;i<sizeof fog;++i) fog[i]=(uint8_t)i;
    state->shade_maps=shades;state->fixed_maps=fixed;state->fog_maps=fog;
    state->textures[1].data=texture;state->textures[1].size=sizeof texture;
    state->textures[1].width=64;state->textures[1].height=64;
    memset(framebuffer,111,sizeof framebuffer);
    hash=world_hash();
    assert(render_draw(&map,&map.camera,framebuffer)==0);
    assert(state->scene.ray_angles[199] == 26);
    assert(state->scene.ray_angles[200] == (uint16_t)-26);
    assert(state->scene.ray_angles[0] == 8205);
    assert(hash==world_hash());
    for (y=0;y<CM_RENDER_HEIGHT;++y) for (x=0;x<CM_RENDER_WIDTH;++x) {
        if (framebuffer[y*CM_RENDER_WIDTH+x]!=(x>=120 && x<520 && y>=90 && y<390 ? 7:111))
            fprintf(stderr,"room pixel %u,%u=%u commands=%u\n",x,y,framebuffer[y*640+x],state->command_count);
        assert(framebuffer[y*CM_RENDER_WIDTH+x]==
               (x>=120 && x<520 && y>=90 && y<390 ? 7:111));
    }
    assert(render_visibility(&map,&map.camera,&visible)==0);
    assert(visible.count==0 && hash==world_hash());
    for (i=1;i<4;++i) {
        map.camera.angle=(uint16_t)(i*16384);
        assert(render_draw(&map,&map.camera,framebuffer)==0);
        for (y=90;y<390;++y) for (x=120;x<520;++x)
            assert(framebuffer[y*640+x]==7);
    }
    map.camera.angle=0;
    map.objects[1].flags=R_PROP|R_PACKED;
    map.objects[1].x=map.camera.u+16384;
    map.objects[1].y=map.camera.v;
    map.objects[1].z=32768;map.objects[1].height=32768;
    map.objects[1].texture=2;map.objects[1].targetable=1;
    map.omap.nodes[5*32+5].object=1;
    state->textures[2]=state->textures[1];
    state->textures[2].frame_half_w=8192;
    state->textures[2].frame_base=32768;
    hash=world_hash();
    assert(render_visibility(&map,&map.camera,&visible)==0);
    assert(hash==world_hash() && visible.count==1);
    assert(visible.props[0].object==1 && visible.props[0].targetable);
    assert(visible.props[0].left<=320 && visible.props[0].right>=320);
    compare_targets();
    map.objects[1].flags&=~R_PACKED;
    assert(render_visibility(&map,&map.camera,&visible)==0);
    assert(visible.count==1 && !visible.props[0].targetable);
    compare_targets();
    map.objects[1].flags|=R_PACKED;
    map.objects[2]=map.objects[1];map.objects[2].x+=8192;
    map.omap.nodes[5*32+5].next=1024;map.omap.nodes[1024].object=2;
    compare_targets();
    assert(visible.count==2 && visible.props[0].object==2 && visible.props[1].object==1);
    map.objects[2].targetable=0;
    compare_targets();
    map.objects[1].y=((map.camera.v&~255)+128)+8192;
    assert(compare_targets() == 1);
    assert(visible.props[visible.count-1].right==320); /* Inclusive centre edge. */
    map.objects[1].y+=256;
    assert(compare_targets() == 0);
    map.objects[1].y=map.camera.v;
    map.objects[1].x=map.camera.u+1152;map.camera.angle=UINT16_MAX;
    {
        int32_t sx, sy;
        unsigned angle=(uint16_t)(map.camera.angle+16384)>>5;
        int32_t wc=r_mul16(8192,map.trig.cos[angle]),ws=r_mul16(8192,map.trig.sin[angle]);
        assert(render_visibility(&map,&map.camera,&visible)==0);
        assert(r_project(&map,state,map.objects[1].x+wc,map.objects[1].y+ws,0,&sx,&sy,R_NEAR)==1);
        assert(r_project(&map,state,map.objects[1].x-wc,map.objects[1].y-ws,0,&sx,&sy,R_NEAR)==0);
        assert(r_new_point(&map,state,map.objects[1].x-wc,map.objects[1].y-ws,map.camera.angle,0,&sx,&sy)==0);
    }
    assert(compare_targets() == 0); /* One endpoint crosses1024; fallback rejects. */
    map.camera.angle=0;
    map.objects[1].x=map.camera.u+16384;
    map.objects[1].y=map.camera.v+4096;
    for (i=3;i<16;++i) {
        state->textures[i]=state->textures[2];
        state->textures[i].frame_half_w=0;
    }
    map.objects[1].rotations=8;
    assert(render_prop_texture(&map.camera,&map.objects[1])==6);
    state->textures[6].frame_half_w=8192;
    assert(compare_targets() == 1);
    state->textures[6].frame_half_w=0;
    map.objects[1].rotations=-3;
    assert(render_prop_texture(&map.camera,&map.objects[1])==3);
    state->textures[3].frame_half_w=8192;
    assert(compare_targets() == 1);
    state->textures[3].frame_half_w=0;
    map.objects[1].rotations=-9;
    assert(render_prop_texture(&map.camera,&map.objects[1])==7);
    state->textures[7].frame_half_w=8192;
    assert(compare_targets() == 1);
    {
        const uint8_t data[]={4,0,12,13,14,15};
        const uint8_t bad[]={4,0,12};
        const uint8_t masked[]={254,255,2,0,12,13};
        uint8_t increment[256];
        render_texture t={0};
        t.data=data;t.size=sizeof data;t.width=2;t.height=1;
        state->framebuffer=framebuffer;
        assert(r_column(state,&t,0,0,0,4,65536,0,shades,2)==0);
        for (i=0;i<4;++i) assert(framebuffer[(90+i)*640+120]==12+i);
        t.data=bad;t.size=sizeof bad;
        assert(r_column(state,&t,0,0,0,4,65536,0,shades,2)!=0);
        t.data=masked;t.size=sizeof masked;
        for (i=0;i<4;++i) framebuffer[(90+i)*640+120]=99;
        assert(r_column(state,&t,0,0,0,4,65536,0,shades,2)==0);
        assert(framebuffer[90*640+120]==99 && framebuffer[91*640+120]==99);
        assert(framebuffer[92*640+120]==12 && framebuffer[93*640+120]==13);
        for (i=0;i<256;++i) {
            increment[i]=(uint8_t)(i+1);
            fixed[9*256+i]=(uint8_t)(i^128);
        }
        t.data=data;t.size=sizeof data;state->parity=0;
        for (i=0;i<4;++i) framebuffer[(90+i)*640+120]=20;
        assert(r_column(state,&t,0,0,0,4,65536,0,increment,3)==0);
        for (i=0;i<4;++i) assert(framebuffer[(90+i)*640+120]==(i&1 ? 20:21));
        assert(r_column(state,&t,0,0,0,4,65536,0,shades,4)==0);
        for (i=0;i<4;++i) assert(framebuffer[(90+i)*640+120]==((12+i)^(i&1 ? 0:128)));
    }
    memset(map.objects,0,sizeof map.objects);memset(&map.omap,0,sizeof map.omap);
    map.map_id_texture[2]=3;map.map_id_texture[3]=4;
    memset(floor_texture,5,sizeof floor_texture);memset(ceiling_texture,9,sizeof ceiling_texture);
    state->textures[3]=state->textures[1];state->textures[3].data=floor_texture;
    state->textures[4]=state->textures[1];state->textures[4].data=ceiling_texture;
    for (y=0;y<32;++y) for (x=0;x<32;++x) {
        world_cell *cell=&map.cells[0][y*32+x];
        cell->wall_sides=UINT32_MAX;cell->floor=cell->ceiling=0;
        if (x>=3 && x<=7 && y>=3 && y<=7) {
            cell->wall_sides=(x==7 ? 1u:0)|(y==7 ? 256u:0)|
                             (x==3 ? 65536u:0)|(y==3 ? 16777216u:0);
            cell->floor=2;cell->ceiling=3;
        }
    }
    assert(render_draw(&map,&map.camera,framebuffer)==0);
    assert(framebuffer[100*640+320]==9);
    assert(framebuffer[240*640+320]==7);
    assert(framebuffer[380*640+320]==5);
    map.shade_override.bias=7;map.shade_override.shift=13;map.shade_override.scale=2;
    map.shade_override.revision=1;map.fog_on=1;
    assert(render_visibility(&map,&map.camera,&visible)==0);
    assert(state->shade_bias==7 && state->shade_shift==13 && state->shade_scale==2 && state->fog_on==1);
    {
        const uint8_t raw[]={1,2,3,4};
        const uint8_t replacement[]={4,3,2,1};
        const uint8_t packed[]={8,0,0,0,12,0,0,0,2,0,1,2,2,0,3,4};
        world_texture *t=&map.textures[0];
        t->kind=WORLD_TEX_PROP;t->width=t->height=1;t->rotate=1;
        t->pixels.data=raw;t->pixels.size=sizeof raw;map.texture_count=1;
        assert(r_sync_textures(&map,state)==0);
        assert(state->textures[0].data[0]==2 && state->textures[0].data[1]==4);
        assert(state->textures[0].data[2]==1 && state->textures[0].data[3]==3);
        t->pixels.data=replacement;t->revision=1;
        assert(r_sync_textures(&map,state)==0 && state->decoded_bytes==4);
        assert(state->textures[0].data[0]==3 && state->textures[0].data[1]==1);
        t=&map.textures[1];t->kind=WORLD_TEX_ANIM;t->width=t->height=1;t->packed=1;
        t->pixels.data=packed;t->pixels.size=sizeof packed;map.texture_count=2;
        assert(r_sync_textures(&map,state)==0 && state->textures_loaded==2);
        state->framebuffer=framebuffer;
        assert(r_column(state,&state->textures[1],8,0,0,2,65536,0,shades,2)==0);
        assert(framebuffer[90*640+120]==1 && framebuffer[91*640+120]==2);
        t=&map.textures[2];*t=map.textures[1];t->pixels.size=4;map.texture_count=3;
        assert(r_sync_textures(&map,state)!=0);
    }
    return 0;
}
