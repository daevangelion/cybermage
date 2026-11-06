#include "internal.h"

static int32_t distance(const world *w,vis_state *s,unsigned object)
{
    if (!s->distance_seen[object]) {
        const world_object *o=vis_object(w,s,object);
        s->object_distance[object]=cm_distance(s->camera.u,s->camera.v,o->x,o->y);
        s->distance_seen[object]=1;
    }
    return s->object_distance[object];
}

/* FxoVSFxo59934/FxoVSProp59D28. Compare in the larger wall's local
 * coordinates. These are the original extent/distance heuristics, not a
 * Euclidean sort; 45-degree ties deliberately retain the previous order. */
static int wall_point(const world *w,const vis_state *s,const world_object *wall,
                      const world_object *point,int isprop,int result)
{
    int32_t wx=wall->x,wy=wall->y,px=point->x,py=point->y;
    int32_t cu=s->camera.u,cv=s->camera.v,lo,hi,pr=isprop ? point->radius:0;
    int32_t pmin,pmax,distance;
    int opposite,flip;
    cm_fixed_rotate(&w->trig,&wx,&wy,cu,cv,(uint16_t)-wall->heading);
    cm_fixed_rotate(&w->trig,&px,&py,cu,cv,(uint16_t)-wall->heading);
    lo=vis_sub(wy,wall->radius);hi=vis_add(wy,wall->radius);
    pmin=vis_sub(py,pr);pmax=vis_add(py,pr);
    opposite=cu>wx ? px<wx:px>wx;
    distance=vis_abs(vis_sub(cu,isprop ? px:wx));
    if (opposite) {
        if (wy<cv) flip=hi>=cv || pmin<hi || distance>vis_sub(cv,hi);
        else flip=lo<=cv || pmax>lo || distance>vis_sub(lo,cv);
    } else {
        if (wy<cv) flip=lo>pmax || (hi>cv && hi<pmin);
        else flip=hi<pmin || (lo<cv && lo>pmax);
    }
    return result^flip;
}

typedef struct {int32_t xmin,xmax,ymin,ymax,x,y;} bounds;

/* FxoVSFlat5A430 and FlatVSProp5AA69: rectangle ordering shares the
 * same16-unit edge bias, with line endpoints for FX and radius for props. */
static int rectangle(const vis_state *s,const world_object *flat,bounds b)
{
    int32_t xmin=vis_sub(flat->x,flat->radius),xmax=vis_add(flat->x,flat->radius);
    int32_t ymin=vis_sub(flat->y,flat->height),ymax=vis_add(flat->y,flat->height);
    int32_t cu=s->camera.u,cv=s->camera.v;
    if (cu>=xmin && cu<=xmax && cv>=ymin && cv<=ymax) return 1;
    if (xmax<cu) {
        if (b.x<vis_sub(xmax,16)) {
            if (b.xmax<xmin) return 1;
            if (ymin<cv) {
                if (ymax>cv || b.y<vis_sub(ymax,16)) return 1;
                if (b.ymin<cv && b.xmax>xmin) return 0;
                return vis_sub(cu,b.xmax)>vis_sub(cv,ymax);
            }
            if (b.y>vis_add(ymin,16)) return 1;
            if (b.ymax>cv && b.xmax>xmin) return 0;
            return vis_sub(cu,b.xmax)>vis_sub(ymin,cv);
        }
        if (ymin<cv) return ymin>b.ymax || (ymax>cv && b.ymin>ymax);
        return b.ymin>ymax;
    }
    if (xmin>cu) {
        if (b.x>vis_add(xmin,16)) {
            if (b.xmin>xmax) return 1;
            if (ymin<cv) {
                if (ymax>cv || b.y<vis_sub(ymax,16)) return 1;
                if (b.ymin<cv && b.xmin<xmax) return 0;
                return vis_sub(b.xmin,cu)>vis_sub(cv,ymax);
            }
            if (b.y>vis_add(ymin,16)) return 1;
            if (b.ymax>cv && b.xmin<xmax) return 0;
            return vis_sub(b.xmin,cu)>vis_sub(ymin,cv);
        }
        if (ymin<cv) return ymin>b.ymax || (ymax>cv && b.ymin>ymax);
        return b.ymin>ymax;
    }
    if (b.xmax<xmin || b.xmin>xmax) return 1;
    if (ymin<cv) {
        if (b.y<vis_sub(ymax,16)) return 1;
        return b.ymin>cv && vis_sub(b.ymin,cv)>vis_sub(cv,ymax);
    }
    if (b.y>vis_add(ymin,16)) return 1;
    return b.ymax<cv && vis_sub(cv,b.ymax)>vis_sub(ymin,cv);
}

int vis_before(const world *w,vis_state *s,unsigned a,unsigned b)
{
    const world_object *oa=vis_object(w,s,a),*ob=vis_object(w,s,b),*flat,*other;
    unsigned ka=s->object_kind[a],kb=s->object_kind[b];
    int result,flip=0;
    bounds box;
    if (ka==kb) {
        if (ka) return distance(w,s,a)>=distance(w,s,b);
        return oa->radius>ob->radius ? wall_point(w,s,oa,ob,0,1):wall_point(w,s,ob,oa,0,0);
    }
    if (ka!=1 && kb!=1)
        return ka==0 ? wall_point(w,s,oa,ob,1,1):wall_point(w,s,ob,oa,1,0);
    flat=ka==1 ? oa:ob;other=ka==1 ? ob:oa;
    result=ka==1;
    box.x=other->x;box.y=other->y;
    if ((ka==0 || kb==0)) {
        box.xmin=vis_min(other->corner_x[0],other->corner_x[1]);
        box.xmax=vis_max(other->corner_x[0],other->corner_x[1]);
        box.ymin=vis_min(other->corner_y[0],other->corner_y[1]);
        box.ymax=vis_max(other->corner_y[0],other->corner_y[1]);
        if (other->x>vis_sub(flat->x,flat->radius) && other->x<vis_add(flat->x,flat->radius) &&
            other->y>vis_sub(flat->y,flat->height) && other->y<vis_add(flat->y,flat->height)) {
            if (vis_sub(other->z,other->height)>=flat->z) return result^(s->camera.z<flat->z);
            if (vis_add(other->z,other->height)<=flat->z) return result^(s->camera.z>flat->z);
        }
    } else {
        box.xmin=vis_sub(other->x,other->radius);box.xmax=vis_add(other->x,other->radius);
        box.ymin=vis_sub(other->y,other->radius);box.ymax=vis_add(other->y,other->radius);
        if (other->x>=vis_sub(flat->x,flat->radius) && other->x<=vis_add(flat->x,flat->radius) &&
            other->y>=vis_sub(flat->y,flat->height) && other->y<=vis_add(flat->y,flat->height))
            return result^((other->floor_z>=flat->z) ? flat->z>s->camera.z:flat->z<s->camera.z);
        if (box.xmin<=vis_add(flat->x,flat->radius) && box.xmax>=vis_sub(flat->x,flat->radius) &&
            box.ymin<=vis_add(flat->y,flat->height) && box.ymax>=vis_sub(flat->y,flat->height)) {
            if (other->floor_z>=flat->z) return result^(s->camera.z<flat->z);
            if (vis_add(vis_add(other->floor_z,other->z),other->height)<flat->z)
                return result^(s->camera.z>flat->z);
        }
    }
    flip=rectangle(s,flat,box);
    return result^flip;
}
