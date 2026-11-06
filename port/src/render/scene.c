#include "internal.h"

static int project_quad(const world *w,const render_state *s,render_quad *q,
                        const int32_t px[4],const int32_t py[4],uint16_t angle,
                        int32_t bottom,int32_t top)
{
    unsigned pair;
    for (pair=0;pair<4;pair+=2) {
        unsigned i=pair,j=pair+1;
        int32_t h=pair ? top:bottom;
        int a=r_project(w,s,px[i],py[i],h,&q->x[i],&q->y[i],R_NEAR);
        int b=r_project(w,s,px[j],py[j],h,&q->x[j],&q->y[j],R_NEAR);
        if (b && !a) a=r_new_point(w,s,px[i],py[i],angle,h,&q->x[i],&q->y[i]);
        if (a && !b) b=r_new_point(w,s,px[j],py[j],angle,h,&q->x[j],&q->y[j]);
        if (!a || !b) return 0;
    }
    return 1;
}

static int prop_frame(const world *w,const render_state *s,const world_object *o)
{
    int texture=o->texture,rotations=o->rotations;
    uint16_t relative;
    unsigned e;
    if (rotations<=1 && rotations!=-3 && rotations!=-9) return texture;
    relative=(uint16_t)(cm_atan64k(o->x,o->y,s->camera.u,s->camera.v)-o->heading);
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
    (void)w;
    return texture;
}

static int prop(const world *w,render_state *s,unsigned object,unsigned light,
                render_visibility_list *out)
{
    const world_object *o=r_object(w,s,object);
    int texture=prop_frame(w,s,o),i;
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
    if (out) {
        int left=r_min(q->x[0],q->x[1]),right=r_max(q->x[0],q->x[1]);
        int y0=s->horizon-q->y[3],y1=s->horizon-q->y[0];
        if (!(right<0 || left>=s->width || y0>=s->height || y1<0 || y1<y0)) {
            if (out->count>=CM_OBJECT_MAX) return r_fail(s,"visible prop list full",object);
            i=(int)out->count++;
            out->props[i].object=(uint16_t)object;
            out->props[i].left=s->x+r_max(left,0);
            out->props[i].right=s->x+r_min(right,s->width-1);
            out->props[i].targetable=(uint8_t)(o->targetable==1 && (o->flags&R_PACKED));
        }
    }
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
    for (i=0;i<4;++i) q->visible[i]=(uint8_t)r_project(w,s,wx[i],wy[i],h,&q->x[i],&q->y[i],R_NEAR);
    command->object=(uint16_t)object;command->kind=RENDER_FLAT;command->top=(uint8_t)top;
    command->height=h;command->horizon=s->horizon;command->light=(uint8_t)light;
    ++s->command_count;
    s->horizon=old_horizon;
    return 0;
}

int r_scene(const world *w,render_state *s,render_visibility_list *out)
{
    int cell=s->cell_head;
    while (cell>=0) {
        unsigned id=s->cells[cell].cell,light=w->cells[id>>10][id&1023].light;
        int node=s->object_head[id];
        uint32_t info=s->cells[cell].info;
        while (node>=0) {
            unsigned object=s->nodes[node].object;
            const world_object *o=r_object(w,s,object);
            int draw=0,iswall=0;
            if (o->flags&R_WALL) {draw=1;iswall=1;}
            else if (o->flags&R_FXO) {draw=--s->object_count[object]==0;iswall=1;}
            else if (o->flags&R_PROP) {
                if (o->floor_z>s->camera.z && (o->flags&UINT32_C(0x00000800))) {
                    unsigned pl=s->prop_level[object];
                    if (s->object_count[object] && (info>>24)<=(pl>>8)+1 && ((info>>16)&255)==(pl&255)) {
                        draw=1;s->object_count[object]=0;
                    }
                } else draw=--s->object_count[object]==0;
                if (draw && prop(w,s,object,light,out)) return -1;
                draw=0;
            } else if (o->flags&R_FLAT) {
                if (o->z<s->camera.z && (o->flags&R_FLOOR)) {if (flat(w,s,object,0,light)) return -1;}
                else if (o->z>s->camera.z && (o->flags&R_TOP)) {if (flat(w,s,object,1,light)) return -1;}
            }
            if (draw && iswall && wall(w,s,object,light)) return -1;
            node=s->nodes[node].next;
        }
        cell=s->cells[cell].next;
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
