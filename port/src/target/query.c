#include "target.h"
#include "../vis/internal.h"

static vis_state state;

/* DrawPackProp/Invis/Aura append after projection and inclusive XY clipping;
 * DrawMaskProp never appends. Pixel coverage/occlusion is not consulted. */
static int collect(const world *w,vis_state *s,unsigned object,target_list *out)
{
    const world_object *o=&w->objects[object];
    const world_texture *t;
    unsigned ang=(uint16_t)(s->camera.angle+16384)>>5,pair;
    int texture;
    int32_t wc,ws,dc,ds,px[4],py[4],qx[4],qy[4],base,bottom,top;
    int32_t left,right,y0,y1;
    if (o->targetable!=1 || !(o->flags&VIS_PACKED)) return 0;
    texture=vis_prop_texture(&s->camera,o);
    if (texture<0 || (unsigned)texture>=w->texture_count)
        return vis_fail(s,"prop frame index",object);
    t=&w->textures[texture];
    wc=vis_mul16(t->half_w,w->trig.cos[ang]);ws=vis_mul16(t->half_w,w->trig.sin[ang]);
    dc=vis_mul16(t->depth,w->trig.cos[ang]);ds=vis_mul16(t->depth,w->trig.sin[ang]);
    px[0]=px[3]=vis_sub(vis_add(o->x,wc),dc);px[1]=px[2]=vis_sub(vis_sub(o->x,wc),dc);
    py[0]=py[3]=vis_sub(vis_add(o->y,ws),ds);py[1]=py[2]=vis_sub(vis_sub(o->y,ws),ds);
    base=vis_sub(vis_add(o->z,o->floor_z),s->camera.z);
    bottom=vis_mul(vis_sub(vis_sub(base,t->base),t->frame_height),s->focal);
    top=vis_mul(vis_sub((o->flags&UINT32_C(0x00001000)) ? base:vis_add(base,t->base),t->frame_height),s->focal);
    for (pair=0;pair<4;pair+=2) {
        unsigned j=pair+1;
        int32_t h=pair ? top:bottom;
        int a=vis_project(s,px[pair],py[pair],h,&qx[pair],&qy[pair],VIS_NEAR);
        int b=vis_project(s,px[j],py[j],h,&qx[j],&qy[j],VIS_NEAR);
        if (b && !a) a=vis_new_point(w,s,px[pair],py[pair],s->camera.angle,h,&qx[pair],&qy[pair]);
        if (a && !b) b=vis_new_point(w,s,px[j],py[j],s->camera.angle,h,&qx[j],&qy[j]);
        if (!a || !b) return 0;
    }
    left=vis_min(qx[0],qx[1]);right=vis_max(qx[0],qx[1]);
    y0=vis_sub(s->horizon,qy[3]);y1=vis_sub(s->horizon,qy[0]);
    if (right<0 || left>=s->width || y0>=s->height || y1<0 || y1<y0) return 0;
    left=vis_add(s->x,vis_max(left,0));right=vis_add(s->x,vis_min(right,s->width-1));
    if (left<=CM_TARGET_COLUMN && right>=CM_TARGET_COLUMN) {
        if (out->count>=CM_OBJECT_MAX) return vis_fail(s,"target list full",object);
        out->objects[out->count++]=(uint16_t)object;
    }
    return 0;
}

int target_query(target_list *out,const world *w,const world_camera *camera,
                 const vis_view *view)
{
    vis_state *s=&state;
    int cell;
    if (!out || !w || !camera || !view) return cm_fail("target",0,"null query input");
    out->count=0;
    if (vis_prepare(w,camera,view,s) || vis_build_lists(w,s)) return -1;
    cell=s->cell_head;
    while (cell>=0) {
        unsigned id=s->cells[cell].cell;
        int node=s->object_head[id];
        uint32_t info=s->cells[cell].info;
        while (node>=0) {
            unsigned object=s->nodes[node].object;
            const world_object *o=vis_object(w,s,object);
            if (!(o->flags&(VIS_WALL|VIS_FXO)) && (o->flags&VIS_PROP)) {
                int draw;
                if (o->floor_z>s->camera.z && (o->flags&UINT32_C(0x00000800))) {
                    unsigned pl=s->prop_level[object];
                    draw=s->object_count[object] && (info>>24)<=(pl>>8)+1 &&
                         ((info>>16)&255)==(pl&255);
                    if (draw) s->object_count[object]=0;
                } else draw=--s->object_count[object]==0;
                if (draw && collect(w,s,object,out)) {out->count=0;return -1;}
            }
            node=s->nodes[node].next;
        }
        cell=s->cells[cell].next;
    }
    return 0;
}
