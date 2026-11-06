#include "internal.h"

static int project_quad(const world *w,const render_state *s,render_quad *q,
                        const int32_t px[4],const int32_t py[4],uint16_t angle,
                        int32_t bottom,int32_t top)
{
    unsigned pair;
    for (pair=0;pair<4;pair+=2) {
        unsigned i=pair,j=pair+1;
        int32_t h=pair ? top:bottom;
        int a=vis_project(&s->scene,px[i],py[i],h,&q->x[i],&q->y[i],R_NEAR);
        int b=vis_project(&s->scene,px[j],py[j],h,&q->x[j],&q->y[j],R_NEAR);
        if (b && !a) a=vis_new_point(w,&s->scene,px[i],py[i],angle,h,&q->x[i],&q->y[i]);
        if (a && !b) b=vis_new_point(w,&s->scene,px[j],py[j],angle,h,&q->x[j],&q->y[j]);
        if (!a || !b) return 0;
    }
    return 1;
}

static int prop(const world *w,render_state *s,unsigned object,unsigned light)
{
    const world_object *o=r_object(w,s,object);
    int texture=vis_prop_texture(&s->camera,o);
    unsigned ang=(uint16_t)(s->camera.angle+16384)>>5;
    int32_t wc,ws,dc,ds,px[4],py[4],base,bottom,top;
    const render_texture *t;
    render_command *command;
    render_quad *q;
    if (s->command_count>=CM_OBJECT_MAX) return r_fail(s,"render command list full",object);
    command=&s->commands[s->command_count];q=&command->quad;
    if (texture<0 || texture>=CM_RENDER_TEXTURES) return r_fail(s,"prop frame index",object);
    t=&s->textures[texture];
    if (!t->data) return r_fail(s,"prop frame not loaded",object);
    wc=r_mul16(t->frame_half_w,w->trig.cos[ang]);ws=r_mul16(t->frame_half_w,w->trig.sin[ang]);
    dc=r_mul16(t->frame_depth,w->trig.cos[ang]);ds=r_mul16(t->frame_depth,w->trig.sin[ang]);
    px[0]=px[3]=r_sub(r_add(o->x,wc),dc);px[1]=px[2]=r_sub(r_sub(o->x,wc),dc);
    py[0]=py[3]=r_sub(r_add(o->y,ws),ds);py[1]=py[2]=r_sub(r_sub(o->y,ws),ds);
    base=r_sub(r_add(o->z,o->floor_z),s->camera.z);
    bottom=r_mul(r_sub(r_sub(base,t->frame_base),t->frame_height),s->focal);
    top=r_mul(r_sub((o->flags&UINT32_C(0x00001000)) ? base:r_add(base,t->frame_base),t->frame_height),s->focal);
    if (!project_quad(w,s,q,px,py,s->camera.angle,bottom,top)) return 0;
    command->object=(uint16_t)object;command->texture=(uint16_t)texture;
    command->kind=RENDER_PROP;command->light=(uint8_t)light;command->horizon=s->horizon;
    ++s->command_count;
    return 0;
}

static int wall(const world *w,render_state *s,unsigned object,unsigned light)
{
    const world_object *o=r_object(w,s,object);
    int32_t base=r_sub(o->z,s->camera.z);
    render_command *command;
    if (s->command_count>=CM_OBJECT_MAX) return r_fail(s,"render command list full",object);
    command=&s->commands[s->command_count];
    if (!project_quad(w,s,&command->quad,o->corner_x,o->corner_y,o->heading,
                      r_mul(r_sub(base,o->height),s->focal),
                      r_mul(r_add(base,o->height),s->focal))) return 0;
    command->object=(uint16_t)object;command->kind=RENDER_WALL;
    command->light=(uint8_t)light;command->horizon=s->horizon;
    ++s->command_count;return 0;
}

static int flat(const world *w,render_state *s,unsigned object,int top,unsigned light)
{
    const world_object *o=r_object(w,s,object);
    render_command *command;
    render_quad *q;
    int32_t h=r_mul(top ? r_sub(o->z,s->camera.z):r_sub(s->camera.z,o->z),s->focal);
    int32_t wx[4],wy[4];
    int old_horizon=s->horizon;
    unsigned i;
    if (s->command_count>=CM_OBJECT_MAX) return r_fail(s,"render command list full",object);
    command=&s->commands[s->command_count];q=&command->quad;
    if (top) s->horizon=s->height/2+r_mul(s->camera.pitch,s->height/2)/240;
    wx[0]=wx[3]=r_sub(o->x,o->radius);wx[1]=wx[2]=r_add(o->x,o->radius);
    wy[0]=wy[1]=r_sub(o->y,o->height);wy[2]=wy[3]=r_add(o->y,o->height);
    for (i=0;i<4;++i) q->visible[i]=(uint8_t)vis_project(&s->scene,wx[i],wy[i],h,&q->x[i],&q->y[i],R_NEAR);
    command->object=(uint16_t)object;command->kind=RENDER_FLAT;command->top=(uint8_t)top;
    command->height=h;command->horizon=s->horizon;command->light=(uint8_t)light;
    ++s->command_count;
    s->horizon=old_horizon;
    return 0;
}

int r_scene(const world *w,render_state *s)
{
    int cell=s->scene.cell_head;
    while (cell>=0) {
        unsigned id=s->scene.cells[cell].cell,light=w->cells[id>>10][id&1023].light;
        int node=s->scene.object_head[id];
        uint32_t info=s->scene.cells[cell].info;
        while (node>=0) {
            unsigned object=s->scene.nodes[node].object;
            const world_object *o=r_object(w,s,object);
            int draw=0,iswall=0;
            if (o->flags&R_WALL) {draw=1;iswall=1;}
            else if (o->flags&R_FXO) {draw=--s->scene.object_count[object]==0;iswall=1;}
            else if (o->flags&R_PROP) {
                if (o->floor_z>s->camera.z && (o->flags&UINT32_C(0x00000800))) {
                    unsigned pl=s->scene.prop_level[object];
                    if (s->scene.object_count[object] && (info>>24)<=(pl>>8)+1 && ((info>>16)&255)==(pl&255)) {
                        draw=1;s->scene.object_count[object]=0;
                    }
                } else draw=--s->scene.object_count[object]==0;
                if (draw && prop(w,s,object,light)) return -1;
                draw=0;
            } else if (o->flags&R_FLAT) {
                if (o->z<s->camera.z && (o->flags&R_FLOOR)) {if (flat(w,s,object,0,light)) return -1;}
                else if (o->z>s->camera.z && (o->flags&R_TOP)) {if (flat(w,s,object,1,light)) return -1;}
            }
            if (draw && iswall && wall(w,s,object,light)) return -1;
            node=s->scene.nodes[node].next;
        }
        cell=s->scene.cells[cell].next;
    }
    if (!s->error) s->visible_world=w;
    return s->error;
}

int r_paint(const world *w,render_state *s)
{
    unsigned i;
    int horizon=s->horizon;
    for (i=0;i<s->command_count;++i) {
        render_command *c=&s->commands[i];
        const world_object *o=r_object(w,s,c->object);
        int result;
        s->horizon=c->horizon;
        if (c->kind==RENDER_WALL) result=r_draw_wall(w,s,o,&c->quad,c->light);
        else if (c->kind==RENDER_PROP) result=r_draw_prop(w,s,o,&c->quad,c->texture,c->light);
        else result=r_draw_flat(w,s,o,&c->quad,c->height,c->top,c->light);
        if (result) {s->horizon=horizon;return -1;}
    }
    s->horizon=horizon;
    return 0;
}
