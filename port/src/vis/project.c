#include "internal.h"

/* RWCPlot062C28: screen-x multiply wraps32 before division. */
int vis_project(const vis_state *s,int32_t x,int32_t y,int32_t h,
                int32_t *sx,int32_t *sy,int32_t near)
{
    int32_t dx=vis_sub(x,s->camera.u),dy=vis_sub(y,s->camera.v),rx,ry;
    rx=vis_sub(vis_mul16(s->camera_cos,dx),vis_mul16(s->camera_sin,dy));
    ry=vis_add(vis_mul16(s->camera_sin,dx),vis_mul16(s->camera_cos,dy));
    if (rx<=near) return 0;
    *sx=vis_sub(s->width/2,vis_mul(ry,s->focal)/rx);*sy=h/rx;
    return 1;
}

int vis_new_point(const world *w,const vis_state *s,int32_t x,int32_t y,
                  uint16_t angle,int32_t h,int32_t *sx,int32_t *sy)
{
    uint16_t ba=(uint16_t)(s->camera.angle-angle);
    unsigned q=(ba&16383)>>3;
    int32_t rx,t;
    cm_fixed_rotate(&w->trig,&x,&y,s->camera.u,s->camera.v,(uint16_t)-angle);
    rx=vis_sub(x,s->camera.u);
    t=w->trig.tan[(ba&16384) ? q : 2047-q];
    y=(ba&16384) ? vis_add(vis_mul16(rx,t),s->camera.v) : vis_sub(s->camera.v,vis_mul16(rx,t));
    y=vis_add(y,ba<32768 ? 2048 : -2048);
    cm_fixed_rotate(&w->trig,&x,&y,s->camera.u,s->camera.v,angle);
    return vis_project(s,x,y,h,sx,sy,256);
}

int vis_prop_texture(const world_camera *camera,const world_object *o)
{
    int texture=o->texture,rotations=o->rotations;
    uint16_t relative;
    unsigned e;
    if (rotations<=1 && rotations!=-3 && rotations!=-9) return texture;
    relative=(uint16_t)(cm_atan64k(o->x,o->y,camera->u,camera->v)-o->heading);
    if (rotations>1) {
        relative=(uint16_t)(relative+32768/rotations);
        texture+=relative/(65536/rotations);
    } else if (rotations==-3) {
        e=(uint16_t)(relative+4096)>>13;
        if (e) texture+=e<5 ? 1:(o->prop_kind==3 ? 2:7);
    } else {
        e=(uint16_t)(relative+2048)>>12;
        if (e) texture+=(e>4 && e<14) ? 5:(e>13 ? e-8:e);
    }
    return texture;
}
