#include "internal.h"
#include <math.h>
#include <string.h>

static render_state state;

render_state *render_get_state(void) { return &state; }

int render_set_view(int x,int y,int width,int height)
{
    int i;
    double a, step;
    /* MakeRayAngles 58E94: accumulator, not a per-column atan formula. */
    const double pi = 3.14159265358979323846;
    if (x < 0 || y < 0 || width < 2 || height < 2 || (width & 1) ||
        x > CM_RENDER_WIDTH-width || y > CM_RENDER_HEIGHT-height)
        return cm_fail("render_sw",0,"invalid view rectangle");
    state.x=x; state.y=y; state.width=width; state.height=height;
    state.visible_world=NULL;
    state.focal=width/2;
    step=65536.0/width; a=step*0.5;
    for (i=width/2-1;i>=0;--i) {
        int angle;
        if (!i) a+=step;
        angle=(int)(atan(a/32768.0)*32768.0/pi+0.5);
        state.ray_angles[i]=(uint16_t)angle;
        state.ray_angles[width-i-1]=(uint16_t)-angle;
        a+=step;
    }
    return 0;
}

int render_shade_distance(const render_state *s,int32_t distance)
{
    int32_t value=r_mul(r_sub(distance >> ((unsigned)s->shade_shift&31),s->shade_bias),s->shade_scale);
    return r_clamp(value,0,63);
}

int render_fog_distance(int32_t distance)
{
    return r_clamp(r_add(distance >> 14,2),0,16);
}

const uint8_t *r_colormap(const world *w,render_state *s,unsigned light,int32_t distance,int *fixed)
{
    int param;
    unsigned curve=0;
    *fixed=0;
    if (!s->shade_maps || !s->fixed_maps || !s->fog_maps) {
        r_fail(s,"missing colormaps",light); return NULL;
    }
    if (light >= 32) { r_fail(s,"invalid light index",light); return NULL; }
    param=w->light_param[light];
    if (light) {
        unsigned type=w->light_type[light];
        if (type <= 5) {
            *fixed=1;
            if (type==0 && param>=0 && param<64) return s->shade_maps+param*256;
            if (type==1 && param>=0 && param<21) return s->fog_maps+param*256;
            if (type>=2) return s->fixed_maps+(type-2)*256;
            r_fail(s,"invalid fixed light parameter",light); return NULL;
        }
        if (type!=6) { r_fail(s,"invalid light type",light); return NULL; }
        if (light>=16) { r_fail(s,"invalid light curve index",light); return NULL; }
        curve=light;
    }
    if (s->curve!=(int32_t)curve) {
        s->curve=(int32_t)curve;
        s->shade_bias=w->curve_base[curve]; s->shade_shift=w->curve_shift[curve];
        s->shade_scale=w->curve_scale[curve];
    }
    if (!light && s->fog_on) return s->fog_maps+render_fog_distance(distance)*256;
    return s->shade_maps+render_shade_distance(s,distance)*256;
}

int r_prepare(const world *w,const world_camera *c,render_state *s)
{
    unsigned a,f,l,r;
    if (!s->width && render_set_view(120,90,400,300)) return -1;
    if (r_sync_textures(w,s)) return -1;
    if (c->u<0 || c->v<0 || c->u>=CM_MAP_SIDE*R_FIXED_ONE ||
        c->v>=CM_MAP_SIDE*R_FIXED_ONE || c->z<0 || c->z>=CM_MAP_LEVELS*R_FIXED_ONE ||
        c->pitch < -240 || c->pitch > 240)
        return r_fail(s,"camera outside map",0);
    s->camera=*c;
    s->camera.u=(c->u&~255)+128; s->camera.v=(c->v&~255)+128;
    s->horizon=s->height/2-r_mul(c->pitch,s->height/2)/240;
    a=(uint16_t)-c->angle>>5;
    f=c->angle>>5; l=(uint16_t)(c->angle+16384)>>5; r=(uint16_t)(c->angle-16384)>>5;
    s->camera_cos=w->trig.cos[a]; s->camera_sin=w->trig.sin[a];
    s->u_cos=w->trig.cos[f]; s->v_cos=w->trig.sin[f];
    s->u_sin=w->trig.cos[l]; s->v_sin=w->trig.sin[l];
    s->u_step=w->trig.cos[r]; s->v_step=w->trig.sin[r];
    s->generated_count=s->node_count=s->cell_count=s->command_count=0;
    s->visible_world=NULL;
    s->cell_head=-1; s->error=0;
    memset(s->object_head,255,sizeof s->object_head);
    memset(s->step_first,255,sizeof s->step_first);
    memset(s->object_count,0,sizeof s->object_count);
    memset(s->distance_seen,0,sizeof s->distance_seen);
    memset(s->tile_seen,0,sizeof s->tile_seen);
    memset(s->tile_block,0,sizeof s->tile_block);
    memset(s->side_done,0,sizeof s->side_done);
    memset(s->object_done,0,sizeof s->object_done);
    memset(s->floor_done,0,sizeof s->floor_done);
    memset(s->ceiling_done,0,sizeof s->ceiling_done);
    memset(s->trans_done,0,sizeof s->trans_done);
    memset(s->above_seen,0,sizeof s->above_seen);
    memset(s->below_seen,0,sizeof s->below_seen);
    s->level=(unsigned)c->z>>16;
    s->level_bit=1u<<s->level;
    a=((unsigned)c->z&65535)>>14;
    s->depth_above=2u<<a; s->depth_below=2u<<(3-a);
    return 0;
}

int render_visibility(const world *w,const world_camera *c,render_visibility_list *out)
{
    if (!w || !c || !out) return cm_fail("render_sw",0,"null visibility input");
    out->count=0;
    if (r_prepare(w,c,&state) || r_build_lists(w,&state)) return -1;
    state.framebuffer=NULL;
    return r_scene(w,&state,out);
}

static void fill_rows(render_state *s,int first,int last,uint8_t color)
{
    int y;
    for (y=r_max(first,0);y<r_min(last,s->height);++y)
        memset(s->framebuffer+(s->y+y)*CM_RENDER_WIDTH+s->x,color,(size_t)s->width);
}

static int backdrop(render_state *s,int rows)
{
    int n=r_min(((s->width<<7)/640)*2,rows),y,x;
    uint32_t step=UINT32_C(0x50000000)/(unsigned)s->width,v=0;
    uint32_t u0=(uint32_t)(0u-s->camera.angle)<<16;
    if (n>0 && (!s->backdrop.data || s->backdrop.size<131072))
        return r_fail(s,"missing backdrop texture",0);
    for (y=0;y<n;++y) {
        int row=rows-y-1;
        uint32_t u=u0,source=UINT32_C(0x1FC00)-((v>>22)<<10);
        if (row>=0 && row<s->height) {
            uint8_t *dst=s->framebuffer+(s->y+row)*640+s->x;
            for (x=0;x<s->width;++x) {dst[x]=s->backdrop.data[source+(u>>22)];u+=step;}
        }
        v+=step;
    }
    if (n<rows) fill_rows(s,0,rows-n,s->sky_fill);
    return 0;
}

static int background(const world *w,render_state *s)
{
    int radius,fill,sky;
    if (!w->sky_type) {
        radius=r_min((s->height/2>>2)*((s->camera.z>>16)+1),s->height/2);
        fill_rows(s,s->horizon-radius,s->horizon+radius+1,0);
        return 0;
    }
    fill=r_clamp(((s->height-s->horizon)/4)*((s->camera.z>>16)+1),0,s->height);
    if (w->sky_type==4) {
        sky=r_clamp(s->horizon+s->height/4,1,s->height-1);
        if (backdrop(s,sky)) return -1;
        fill_rows(s,sky,s->height,0);
    } else if (w->sky_type==3 || (w->sky_type==2 && s->fog_on)) {
        if (fill) fill_rows(s,0,s->horizon+fill,w->sky_type==3 ? 0:s->sky_fog);
    } else {
        if (backdrop(s,s->horizon)) return -1;
        if (fill) fill_rows(s,s->horizon,s->horizon+fill,0);
    }
    return 0;
}

int render_draw(const world *w,const world_camera *c,uint8_t *fb)
{
    if (!w || !c || !fb) return cm_fail("render_sw",0,"null render input");
    if (r_prepare(w,c,&state) || r_build_lists(w,&state) || r_scene(w,&state,NULL)) return -1;
    return render_paint(w,fb);
}

int render_paint(const world *w,uint8_t *fb)
{
    if (!w || !fb) return cm_fail("render_sw",0,"null paint input");
    if (state.visible_world!=w) return cm_fail("render_sw",0,"visibility must precede painting");
    if (state.error) return state.error;
    state.framebuffer=fb; state.parity^=1;
    if (background(w,&state)) return -1;
    return r_paint(w,&state);
}

void render_view(const world *w,const world_camera *c,uint8_t *fb)
{
    (void)render_draw(w,c,fb);
}
