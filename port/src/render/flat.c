#include "internal.h"
#include <string.h>

static unsigned texel(uint32_t u,uint32_t v,unsigned rot)
{
    unsigned a=(u>>25)&127,b=(v>>25)&127;
    if (!rot) return a+b*128;
    if (rot==1) return 127-a+b*128;
    if (rot==2) return b+a*128;
    return b+16383-a*128-127;
}

int r_draw_flat(const world *w,render_state *s,const world_object *o,
                render_quad *q,int32_t h,int top_flat,unsigned light)
{
    render_edge l,r;
    int32_t wx[4],wy[4],poly_top,x0,lx,rx;
    int i,bottom,li=0,ri=0,seg_l=0,seg_r=0,y,masked,fixed;
    const render_texture *t;
    const uint8_t *map;
    if (h<=0) return r_fail(s,"invalid flat height numerator",o->texture);
    if (o->texture>=CM_RENDER_TEXTURES) return r_fail(s,"flat texture index",o->texture);
    t=&s->textures[o->texture];
    if (!t->data || t->size<R_FLAT_BYTES) return r_fail(s,"flat texture truncated",o->texture);
    wx[0]=wx[3]=r_sub(o->x,o->radius);wx[1]=wx[2]=r_add(o->x,o->radius);
    wy[0]=wy[1]=r_sub(o->y,o->height);wy[2]=wy[3]=r_add(o->y,o->height);
    memset(&l,0,sizeof l);memset(&r,0,sizeof r);
    if (!r_connect(w,s,q,wx,wy,h,&l,&r,&poly_top,&x0)) return 0;
    if ((l.last && s->horizon+1>l.end[1]) || (r.last && s->horizon+1>r.end[1])) return 0;
    for (i=1;i<=l.last;++i) if (l.end[i]>l.end[li]) li=i;
    for (i=1;i<=r.last;++i) if (r.end[i]>r.end[ri]) ri=i;
    bottom=r_max(l.end[li],r.end[ri]);
    if (s->horizon+1>bottom || poly_top>s->height-(top_flat ? 0:1)) return 0;
    bottom=r_min(bottom,s->height-(top_flat ? 0:1));
    lx=r_shl(l.slope[0] ? x0:l.x[0],16);
    rx=r_shl(r.slope[0] ? x0:r.x[0],16);
    masked=!top_flat && (o->flags&R_TRANSPARENT);
    map=r_colormap(w,s,light,0,&fixed);if (!map) return -1;
    /* Offscreen rows still advance the original edge walkers. */
    for (y=poly_top;y<=bottom;++y) {
        int xs=r_max(lx>>16,0),xe=r_min(rx>>16,s->width-1),dy=y-s->horizon;
        int row=top_flat ? s->height-y:y;
        if ((seg_l && xs>=s->width) || (seg_r && xe<0)) return 0;
        if (dy>0 && row>=0 && row<s->height && xs<=xe) {
            int32_t distance=h/dy;
            int32_t scale=(int32_t)((uint32_t)distance/(unsigned)s->focal);
            int32_t du=r_mul(scale,s->u_step),dv=r_mul(scale,s->v_step);
            uint32_t u=(uint32_t)r_add(r_add(r_shl(r_add(s->camera.u,r_mul16(distance,s->u_cos)),16),r_mul(distance,s->u_sin)),r_mul(du,xs));
            uint32_t v=(uint32_t)r_add(r_add(r_shl(r_add(s->camera.v,r_mul16(distance,s->v_cos)),16),r_mul(distance,s->v_sin)),r_mul(dv,xs));
            uint8_t *dst=s->framebuffer+(s->y+row)*CM_RENDER_WIDTH+s->x+xs;
            int x;
            if (!fixed) {map=r_colormap(w,s,light,distance,&fixed);if (!map) return -1;}
            for (x=xs;x<=xe;++x) {
                uint8_t pixel=t->data[texel(u,v,masked ? o->heading>>14:0)];
                if (!masked || pixel) *dst=map[pixel];
                ++dst;u+=(uint32_t)du;v+=(uint32_t)dv;
            }
        }
        if (seg_l<3 && y==l.end[seg_l]) ++seg_l;
        if (seg_r<3 && y==r.end[seg_r]) ++seg_r;
        lx=r_add(lx,l.slope[seg_l]);rx=r_add(rx,r.slope[seg_r]);
        if (y==INT_MAX) break;
    }
    return 0;
}
