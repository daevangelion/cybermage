#include "internal.h"
#include <math.h>
#include <string.h>

int vis_prepare(const world *w,const world_camera *c,const vis_view *v,vis_state *s)
{
    unsigned a;
    if (v->x<0 || v->y<0 || v->width<2 || v->height<2 ||
        v->width>640 || v->height>480 || (v->width&1) ||
        v->x>640-v->width || v->y>480-v->height)
        return vis_fail(s,"invalid original view",0);
    if (c->u<0 || c->v<0 || c->u>=CM_MAP_SIDE*VIS_FIXED_ONE ||
        c->v>=CM_MAP_SIDE*VIS_FIXED_ONE || c->z<0 || c->z>=CM_MAP_LEVELS*VIS_FIXED_ONE ||
        c->pitch < -240 || c->pitch > 240)
        return vis_fail(s,"camera outside map",0);
    if (s->width!=v->width) {
        const double pi=3.14159265358979323846;
        double step=65536.0/v->width,angle=step*0.5;
        int i;
        /* MakeRayAngles 58E94, including the asymmetric outermost step. */
        for (i=v->width/2-1;i>=0;--i) {
            int value;
            if (!i) angle+=step;
            value=(int)(atan(angle/32768.0)*32768.0/pi+0.5);
            s->ray_angles[i]=(uint16_t)value;
            s->ray_angles[v->width-i-1]=(uint16_t)-value;
            angle+=step;
        }
    }
    s->x=v->x;s->y=v->y;s->width=v->width;s->height=v->height;s->focal=v->width/2;
    s->horizon=s->height/2-vis_mul(c->pitch,s->height/2)/240;
    s->camera=*c;
    s->camera.u=(c->u&~255)+128;s->camera.v=(c->v&~255)+128;
    a=(uint16_t)-c->angle>>5;
    s->camera_cos=w->trig.cos[a];s->camera_sin=w->trig.sin[a];
    s->generated_count=s->node_count=s->cell_count=0;
    s->cell_head=-1;s->error=0;
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
    s->level=(unsigned)c->z>>16;s->level_bit=1u<<s->level;
    a=((unsigned)c->z&65535)>>14;
    s->depth_above=2u<<a;s->depth_below=2u<<(3-a);
    return 0;
}

