#include "internal.h"

static void corners(const render_quad *q,const int32_t sy[4],int *a,int *b,int *c,int *d)
{
    if (q->x[0]<q->x[1]) {*a=0;*b=1;*c=3;*d=2;}
    else if (q->x[0]>q->x[1]) {*a=1;*b=0;*c=2;*d=3;}
    else {
        *a=sy[0]>sy[1] ? 0:1; *b=1-*a;
        *c=sy[3]<sy[2] ? 3:2; *d=5-*c;
    }
}

int r_draw_wall(const world *w,render_state *s,const world_object *o,
                const render_quad *q,unsigned light)
{
    int a,b,c,d,x,xl,xr,i,mode,fixed;
    int32_t ylo,yhi,slo,shi,wx=o->x,wy=o->y,dxw,dc,sr;
    int32_t bottom,height=o->height,k,va,vb,len=o->radius,bias;
    int32_t sy[4];
    unsigned rel;
    const render_texture *t;
    const uint8_t *map;
    if (o->texture>=CM_RENDER_TEXTURES) return r_fail(s,"wall texture index",o->texture);
    t=&s->textures[o->texture];
    if (t->width<=0 || t->height<=0 || !t->data || height<=0 || len<=0)
        return r_fail(s,"invalid wall geometry or texture",o->texture);
    for (i=0;i<4;++i) sy[i]=r_sub(s->horizon,q->y[i]);
    corners(q,sy,&a,&b,&c,&d); xl=q->x[a]; xr=q->x[b];
    if (xr<0 || xl>=s->width) return 0;
    ylo=r_add(r_shl(sy[c],16),R_FIXED_HALF);
    yhi=r_add(r_shl(sy[a],16),R_FIXED_HALF);
    slo=r_yslope(q->x[c],sy[c],q->x[d],sy[d]);
    shi=r_yslope(q->x[a],sy[a],q->x[b],sy[b]);
    if (xl<0) {ylo=r_add(ylo,r_mul(slo,xl));yhi=r_add(yhi,r_mul(shi,xl));xl=0;}
    xr=r_min(xr,s->width-1);
    cm_fixed_rotate(&w->trig,&wx,&wy,s->camera.u,s->camera.v,(uint16_t)-o->heading);
    bottom=r_sub(o->z,s->camera.z); k=height/t->width;
    if (!k) return r_fail(s,"wall vertical scale underflow",o->texture);
    if (r_add(height,bottom)>=r_mul(height,2)) {va=r_shl(r_mul(height,2)/k,16);vb=0;}
    else if (r_add(height,bottom)<=0) {va=0;vb=r_shl(r_mul(height,2)/k,16);}
    else {va=r_shl(r_add(height,bottom)/k,16);vb=r_shl(r_sub(height,bottom)/k,16);}
    rel=(uint16_t)(s->camera.angle-o->heading)>>5;
    dxw=r_sub(wx,s->camera.u); dc=r_mul16(w->trig.cos[rel],dxw); sr=w->trig.sin[rel];
    bias=(len/t->height)>>1;
    mode=(o->flags&R_TRANSPARENT) ? ((o->flags&R_PACKED) ? R_COLUMN_PACK:R_COLUMN_MASK):R_COLUMN_SOLID;
    map=r_colormap(w,s,light,0,&fixed); if (!map) return -1;
    for (x=xl;x<=xr;++x) {
        int y1=ylo>>16,y2=yhi>>16,h=r_add(r_sub(y2,y1),1);
        if (y1<s->height && y2>=0 && h>0) {
            uint16_t ba=(uint16_t)(s->camera.angle-o->heading+s->ray_angles[x]);
            int32_t uoff=r_mul16(r_abs(dxw),r_tan(w,ba));
            int32_t tpos=r_add(r_sub(r_add(s->camera.v,uoff),wy),len);
            int col=r_mul(r_add(bias,tpos),t->height)/len;
            int above=s->horizon-y1+1,below=y2-s->horizon;
            int yc=r_max(y1,0),ye=r_min(y2,s->height-1),clip=yc-y1;
            uint32_t src;
            if (o->flags&R_MIRROR) col=t->height*2-1-col;
            col=r_clamp(col,0,t->height*2-1);
            if (mode==R_COLUMN_PACK) {
                if ((uint32_t)col>=t->size/4) return r_fail(s,"packed wall column table",(unsigned)col);
                src=r_u32(t->data+(unsigned)col*4);
            } else src=(uint32_t)col*(unsigned)t->width*2;
            if (!fixed) {map=r_colormap(w,s,light,r_add(dc,r_mul16(sr,uoff)),&fixed);if (!map) return -1;}
            if (above<=0 || below<=0 || !va || !vb) {
                int32_t step=r_shl(t->width,17)/h;
                uint32_t pos=(uint32_t)r_add(r_mul(clip,step),(int32_t)((uint32_t)step>>1));
                if (r_column(s,t,src,x,yc,ye-yc+1,(uint32_t)step,pos,map,mode)) return -1;
            } else {
                int32_t st1=va/above,st2=vb/below;
                uint32_t p1=(uint32_t)r_sub(r_sub(va,(int32_t)((uint32_t)st1>>1)),r_mul(s->horizon-yc,st1));
                uint32_t p2=(uint32_t)r_add((int32_t)((uint32_t)st2>>1),va);
                if (r_column(s,t,src,x,yc,s->horizon-yc+1,(uint32_t)st1,p1,map,mode) ||
                    r_column(s,t,src,x,s->horizon+1,ye-s->horizon,(uint32_t)st2,p2,map,mode)) return -1;
            }
        }
        ylo=r_sub(ylo,slo); yhi=r_sub(yhi,shi);
    }
    return 0;
}

int r_draw_prop(const world *w,render_state *s,const world_object *o,
                const render_quad *q,unsigned texture,unsigned light)
{
    int a,b,c,d,i,xl,xr,x,lo,hi,h,clip,mode,fixed;
    int32_t us,u,vs,v,wx=o->x,wy=o->y;
    int32_t sy[4];
    const render_texture *t;
    const uint8_t *map;
    if (texture>=CM_RENDER_TEXTURES) return r_fail(s,"prop texture index",texture);
    t=&s->textures[texture];
    if (!t->data || t->width<=0 || t->height<=0) return r_fail(s,"missing prop texture",texture);
    for (i=0;i<4;++i) sy[i]=r_sub(s->horizon,q->y[i]);
    corners(q,sy,&a,&b,&c,&d); xl=q->x[a];xr=q->x[b];lo=sy[c];hi=sy[a];
    if (xr<0 || xl>=s->width || lo>=s->height || hi<0 || hi<lo) return 0;
    h=hi-lo+1; us=r_shl(t->height,17)/(xr-xl+1);
    u=r_sub(r_shl(t->height,17),us/2);
    if (xl<0) {u=r_add(u,r_mul(xl,us));xl=0;}
    xr=r_min(xr,s->width-1);clip=r_max(-lo,0);lo=r_max(lo,0);hi=r_min(hi,s->height-1);
    mode=(o->flags&R_PACKED) ? ((o->flags&UINT32_C(0x00002000)) ? R_COLUMN_AURA : ((o->flags&UINT32_C(0x00020000)) ? R_COLUMN_INVIS:R_COLUMN_PACK)):R_COLUMN_MASK;
    vs=r_shl(t->width,(mode!=R_COLUMN_MASK && (o->flags&UINT32_C(0x00001000))) ? 16:17)/h;
    v=r_add(r_mul(clip,vs),(int32_t)((uint32_t)vs>>1));
    cm_fixed_rotate(&w->trig,&wx,&wy,s->camera.u,s->camera.v,(uint16_t)-s->camera.angle);
    if (mode==R_COLUMN_INVIS || mode==R_COLUMN_AURA) {
        if (!s->shade_maps || !s->fixed_maps) return r_fail(s,"missing prop effect maps",texture);
        map=mode==R_COLUMN_INVIS ? s->shade_maps+r_min(render_shade_distance(s,r_sub(wx,s->camera.u))+16,63)*256:s->shade_maps;
    } else {
        map=r_colormap(w,s,light,r_sub(wx,s->camera.u),&fixed);if (!map) return -1;
    }
    for (x=xl;x<=xr;++x) {
        int col=u>>16;
        uint32_t src;
        if (col<0) return r_fail(s,"negative prop column",texture);
        if (mode==R_COLUMN_MASK) src=(uint32_t)col*(unsigned)t->width*2;
        else {
            if ((uint32_t)col>=t->size/4) return r_fail(s,"packed prop column table",texture);
            src=r_u32(t->data+(unsigned)col*4);
        }
        if (r_column(s,t,src,x,lo,hi-lo+1,(uint32_t)vs,(uint32_t)v,map,mode)) return -1;
        u=r_sub(u,us);
    }
    return 0;
}
