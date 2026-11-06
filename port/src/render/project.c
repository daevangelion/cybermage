#include "internal.h"

int32_t r_tan(const world *w,uint16_t angle)
{
    unsigned q=(angle&16383)>>3;
    int32_t t=w->trig.tan[(angle&16384) ? 2047-q : q];
    return angle&32768 ? r_sub(0,t) : t;
}

int32_t r_xslope(int32_t x1,int32_t y1,int32_t x2,int32_t y2)
{
    int32_t dy=r_abs(r_sub(y1,y2));
    return dy ? (int32_t)((int64_t)r_sub(x1,x2)*R_FIXED_ONE/dy) : 0;
}

int32_t r_yslope(int32_t x1,int32_t y1,int32_t x2,int32_t y2)
{
    int32_t dx=r_abs(r_sub(x1,x2));
    return dx ? (int32_t)((int64_t)r_sub(y1,y2)*R_FIXED_ONE/dx) : 0;
}

/* RWCPlot 62C28: the screen-x multiply is 32-bit, unlike FixedRotate. */
int r_project(const world *w,const render_state *s,int32_t x,int32_t y,int32_t h,int32_t *sx,int32_t *sy,int32_t near)
{
    int32_t dx=r_sub(x,s->camera.u),dy=r_sub(y,s->camera.v),rx,ry;
    (void)w;
    rx=r_sub(r_mul16(s->camera_cos,dx),r_mul16(s->camera_sin,dy));
    ry=r_add(r_mul16(s->camera_sin,dx),r_mul16(s->camera_cos,dy));
    if (rx<=near) return 0;
    *sx=r_sub(s->width/2,r_mul(ry,s->focal)/rx);
    *sy=h/rx;
    return 1;
}

int r_new_point(const world *w,const render_state *s,int32_t x,int32_t y,uint16_t angle,int32_t h,int32_t *sx,int32_t *sy)
{
    uint16_t ba=(uint16_t)(s->camera.angle-angle);
    unsigned q=(ba&16383)>>3;
    int32_t rx,t;
    cm_fixed_rotate(&w->trig,&x,&y,s->camera.u,s->camera.v,(uint16_t)-angle);
    rx=r_sub(x,s->camera.u);
    t=w->trig.tan[(ba&16384) ? q : 2047-q];
    y=(ba&16384) ? r_add(r_mul16(rx,t),s->camera.v) : r_sub(s->camera.v,r_mul16(rx,t));
    y=r_add(y,ba<32768 ? 2048 : -2048);
    cm_fixed_rotate(&w->trig,&x,&y,s->camera.u,s->camera.v,angle);
    return r_project(w,s,x,y,h,sx,sy,256);
}

/* FindPoint 64B4C clips against the fixed DOS plane at row479, even
 * for a smaller view. Preserve quadrant tie breaks and fixed-point products. */
int r_find_point(const world *w,const render_state *s,int32_t xa,int32_t ya,int32_t xb,int32_t h,int32_t *sx,int32_t *sy)
{
    unsigned cam=s->camera.angle,q=(cam&16383)>>3;
    int32_t d=r_max(h/(CM_RENDER_HEIGHT-1),4096),cx,cy,dn,k,rx,ry;
    cx=r_add(s->camera.u,r_mul16(d,w->trig.cos[cam>>5]));
    cy=r_add(s->camera.v,r_mul16(d,w->trig.sin[cam>>5]));
    rx=cx; ry=cy;
    if (xb==xa) {
        dn=r_abs(r_sub(xa,cx));
        k=r_mul16(dn,w->trig.tan[(cam&16384) ? q : 2047-q]);
        if (!(cam&16384)) k=r_sub(0,k);
        if (cx<xa) { rx=r_add(cx,dn); ry=r_add(cy,k); }
        else { rx=r_sub(cx,dn); ry=r_sub(cy,k); }
    } else {
        dn=r_abs(r_sub(ya,cy));
        k=r_mul16(dn,w->trig.tan[(cam&16384) ? 2047-q : q]);
        if (!(cam&16384)) k=r_sub(0,k);
        if (cy<ya) {ry=r_add(cy,dn);rx=r_add(cx,k);}
        else {ry=r_sub(cy,dn);rx=r_sub(cx,k);}
    }
    return r_project(w,s,rx,ry,h,sx,sy,0);
}

static int chain(const world *w,const render_state *s,const render_quad *q,
                 const int32_t wx[4],const int32_t wy[4],int32_t h,
                 unsigned start,int direction,render_edge *e)
{
    int32_t px=q->x[start],py=r_add(q->y[start],s->horizon);
    unsigned prev=start,k;
    e->last=2; e->slope[3]=0;
    for (k=0;k<3;++k) {
        unsigned cc=(start+(unsigned)(direction*(int)(k+1)))&3;
        if (!q->visible[cc]) {
            if (!r_find_point(w,s,wx[cc],wy[cc],wx[prev],h,&e->x[k],&e->end[k])) return 0;
            e->end[k]=r_add(e->end[k],s->horizon);
            e->slope[k]=r_xslope(e->x[k],e->end[k],px,py);
            if (k<2) e->last=(int32_t)k;
            return 1;
        }
        e->x[k]=q->x[cc]; e->end[k]=r_add(q->y[cc],s->horizon);
        e->slope[k]=r_xslope(e->x[k],e->end[k],px,py);
        px=e->x[k]; py=e->end[k]; prev=cc;
    }
    return 1;
}

int r_connect(const world *w,const render_state *s,render_quad *q,
              const int32_t wx[4],const int32_t wy[4],int32_t h,
              render_edge *l,render_edge *r,int32_t *top,int32_t *x0)
{
    unsigned i,start=4;
    for (i=0;i<4;++i)
        if (q->visible[i] && (start==4 || q->y[i]<q->y[start])) start=i;
    if (start==4) return 0;
    *x0=q->x[start]; *top=r_add(q->y[start],s->horizon);
    return chain(w,s,q,wx,wy,h,start,1,l) && chain(w,s,q,wx,wy,h,start,-1,r);
}
