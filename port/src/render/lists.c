#include "internal.h"

static int add_node(render_state *s,unsigned cell,unsigned object,int before)
{
    unsigned n=s->node_count;
    int prev=before<0 ? (s->object_head[cell]<0 ? -1:s->object_tail[cell]) : s->nodes[before].prev;
    if (n>=CM_RENDER_NODES) return r_fail(s,"object list full",n);
    s->nodes[n].object=(uint16_t)object;s->nodes[n].prev=prev;s->nodes[n].next=before;
    if (prev<0) s->object_head[cell]=(int)n;else s->nodes[prev].next=(int)n;
    if (before<0) s->object_tail[cell]=(int)n;else s->nodes[before].prev=(int)n;
    ++s->node_count;return 1;
}

static int create(render_state *s,const world *w,unsigned id,unsigned level,int flat)
{
    unsigned n=s->generated_count,object=CM_RENDER_DYNAMIC_FIRST+n;
    unsigned tx=s->tile&31,ty=s->tile>>5,k;
    world_object *o;
    int32_t x=(int32_t)(tx<<16),y=(int32_t)(ty<<16);
    static const int32_t offset[4][4]={
        {32768,-32768,-32768,32768},{-32768,32768,32768,-32768},
        {-32768,32768,32768,-32768},{32768,-32768,-32768,32768}};
    if (object+1>=CM_OBJECT_MAX) return r_fail(s,"generated object table full",object);
    o=&s->generated[n];
    o->flags=w->map_id_flags[id];o->radius=o->height=R_FIXED_HALF;
    o->texture=w->map_id_texture[id];
    if (flat) {
        o->flags|=flat==2 ? UINT32_C(0x02800000):UINT32_C(0x02440000);
        o->x=x+R_FIXED_HALF;o->y=y+R_FIXED_HALF;
        o->heading=0;
        o->z=flat==2 ? (int32_t)((level+1)<<16)-1:(int32_t)(level<<16);
        s->object_kind[object]=1;
    } else {
        o->flags|=R_WALL;
        o->x=x+(s->side==0 ? 65535:(s->side==2 ? 0:32768));
        o->y=y+(s->side==1 ? 65535:(s->side==3 ? 0:32768));
        o->heading=(uint16_t)(s->side<<14);o->z=(int32_t)(level<<16)+R_FIXED_HALF;
        for (k=0;k<4;++k) {
            o->corner_x[k]=o->x+((s->side&1) ? offset[s->side][k]:0);
            o->corner_y[k]=o->y+((s->side&1) ? 0:offset[s->side][k]);
        }
        s->object_kind[object]=0;
    }
    ++s->generated_count;
    return add_node(s,s->tile+(level<<10),object,s->object_head[s->tile+(level<<10)]);
}

static int insert_object(const world *w,render_state *s,unsigned cell,unsigned obj,int key,int prop)
{
    int n;
    if (s->object_head[cell]<0) return add_node(s,cell,obj,-1);
    if (prop) {
        n=s->object_tail[cell];
        while (n>=0) {
            unsigned other=s->nodes[n].object;
            int before=(s->object_kind[obj]==2 && s->object_kind[other]==2) || r_before(w,s,obj,other);
            if (!before) return add_node(s,cell,obj,s->nodes[n].next);
            n=s->nodes[n].prev;
        }
        return add_node(s,cell,obj,s->object_head[cell]);
    }
    n=s->object_head[cell];
    while (n>=0) {
        unsigned other=s->nodes[n].object;
        if ((int)other==key || r_before(w,s,obj,other)) break;
        n=s->nodes[n].next;
    }
    return add_node(s,cell,obj,n);
}

static int check_objects(const world *w,render_state *s,unsigned level)
{
    unsigned node=s->tile,cell=s->tile+(level<<10),n,loops=0;
    unsigned fx[64],props[64],nf=0,np=0;
    int32_t zlo=(int32_t)(level<<16),zhi=zlo+R_FIXED_ONE;
    int found=0,key=-1;
    while (node) {
        const world_link *link;
        unsigned obj;
        if (node>=CM_CHAIN_NODES || ++loops>CM_CHAIN_NODES) return r_fail(s,"invalid flat chain",node);
        link=&w->flatmap.nodes[node];obj=link->object;
        if (obj>=CM_RENDER_DYNAMIC_FIRST) return r_fail(s,"invalid flat object",obj);
        if (obj && w->objects[obj].z>=zlo && w->objects[obj].z<zhi) {
            s->object_kind[obj]=1;
            if (insert_object(w,s,cell,obj,-1,0)<0) return -1;
            found=1;
        }
        node=link->next;
    }
    node=s->tile;loops=0;
    while (node) {
        const world_link *link;
        unsigned obj;
        const world_object *o;
        int include=0,isfx;
        if (node>=CM_CHAIN_NODES || ++loops>CM_CHAIN_NODES) return r_fail(s,"invalid prop chain",node);
        link=&w->omap.nodes[node];obj=link->object;node=link->next;
        if (!obj || obj==w->player_object) continue;
        if (obj>=CM_RENDER_DYNAMIC_FIRST) return r_fail(s,"invalid prop object",obj);
        o=&w->objects[obj];isfx=(o->flags&R_FXO)!=0;
        if (isfx) include=r_add(o->z,o->height)>zlo && r_sub(o->z,o->height)<zhi;
        else if (!((o->flags&UINT32_C(0x00010000)) && s->steps>=3))
            include=o->floor_z<zhi && r_add(r_add(o->floor_z,o->z),o->height)>=zlo;
        if (!include) continue;
        s->object_kind[obj]=isfx ? 0:2;
        if (!isfx && s->camera.z<o->floor_z && (o->flags&UINT32_C(0x00000800))) {
            unsigned distance=(unsigned)r_abs((int32_t)s->level-(int32_t)level);
            if (!s->object_count[obj]) s->prop_level[obj]=(uint16_t)(s->steps<<8|distance);
            if ((s->prop_level[obj]>>8)>s->steps)
                s->prop_level[obj]=(uint16_t)(s->steps<<8|(s->prop_level[obj]&255));
            if ((unsigned)r_abs((int32_t)s->level-(s->prop_level[obj]&255))>distance)
                s->prop_level[obj]=(uint16_t)((s->prop_level[obj]&65280)|distance);
        }
        ++s->object_count[obj];found=1;
        n=isfx ? nf:np;
        if (n>=64) return r_fail(s,"tile object sublist full",obj);
        if (isfx) {
            while (n && r_before(w,s,obj,fx[n-1])) {fx[n]=fx[n-1];--n;}
            fx[n]=obj;++nf;
        } else {
            while (n && r_before(w,s,obj,props[n-1])) {props[n]=props[n-1];--n;}
            props[n]=obj;++np;
        }
    }
    while (nf) {unsigned obj=fx[--nf];if (insert_object(w,s,cell,obj,key,0)<0) return -1;key=(int)obj;}
    while (np) if (insert_object(w,s,cell,props[--np],-1,1)<0) return -1;
    return found;
}

static int tlist(render_state *s,unsigned level)
{
    unsigned k=s->cell_count;
    int n=s->cell_head,prev=-1;
    uint32_t key=(s->steps<<24)+((unsigned)r_abs((int32_t)s->level-(int32_t)level)<<16)+s->order;
    if (k>=CM_RENDER_CELLS) return r_fail(s,"visible cell list full",k);
    if (s->steps>=sizeof s->step_first/sizeof s->step_first[0])
        return r_fail(s,"ray step list index",s->steps);
    if (s->step_first[s->steps]<0) s->step_first[s->steps]=(int)k;
    else {
        n=s->step_first[s->steps];
        prev=s->cells[n].prev;
    }
    while (n>=0 && key<=s->cells[n].info) {prev=n;n=s->cells[n].next;}
    if (n==s->step_first[s->steps]) s->step_first[s->steps]=(int)k;
    s->cells[k].cell=(uint16_t)(s->tile+(level<<10));s->cells[k].info=key;
    s->cells[k].prev=prev;s->cells[k].next=n;
    if (prev<0) s->cell_head=(int)k;else s->cells[prev].next=(int)k;
    if (n>=0) s->cells[n].prev=(int)k;
    ++s->cell_count;return 0;
}

static int check_plane(const world *w,render_state *s,unsigned level,int above,int *found)
{
    const world_cell *cell=&w->cells[level][s->tile];
    unsigned id=above ? cell->ceiling:cell->floor,bit=1u<<level;
    uint8_t *done=above ? s->ceiling_done:s->floor_done;
    int open=(above ? level<(unsigned)w->top_level:level>0);
    if (id) {
        if (!(done[s->tile]&bit)) {
            done[s->tile]|=(uint8_t)bit;
            if (w->map_id_texture[id]<32768) {
                int result=create(s,w,id,level,above ? 2:1);
                if (result<0) return -1;
                *found=result;
            }
        }
        open=open && (w->map_id_flags[id]&UINT32_C(0x10000000));
    }
    if (open) {
        unsigned adjacent=above ? bit<<1:bit>>1;
        if (above) s->above_open|=bit;else s->below_open|=bit;
        s->blocked&=~adjacent;
        s->tile_block[s->tile]&=~(adjacent<<s->level_shift);
    }
    return 0;
}

static int check_wall(const world *w,render_state *s,unsigned level,int *found)
{
    unsigned id=(w->cells[level][s->tile].wall_sides>>(s->side*8))&255;
    unsigned bit=1u<<level,mask=bit<<s->level_shift;
    if (id) {
        if (w->map_id_texture[id]<32768) {
            int result=create(s,w,id,level,0);
            if (result<0) return -1;
            *found=result;
        }
        if (!(w->map_id_flags[id]&R_TRANSPARENT)) {
            s->tile_block[s->tile]|=mask;s->blocked|=bit;
            return (int)id;
        }
    }
    s->tile_block[s->tile]&=~mask;
    return 0;
}

static int check_vertical(const world *w,render_state *s,unsigned level,int up)
{
    unsigned bit,mask,adjacent;
    int found=0;
    if (level>=CM_MAP_LEVELS || level>(unsigned)w->top_level) return r_fail(s,"vertical ray leaves map",level);
    bit=1u<<level;mask=bit<<s->level_shift;
    if (s->blocked&bit) s->tile_block[s->tile]|=mask;
    else {
        s->tile_seen[s->tile]|=mask;
        if (!(s->object_done[s->tile]&bit)) {
            found=check_objects(w,s,level);if (found<0) return -1;
            s->object_done[s->tile]|=(uint8_t)bit;
        }
        if (check_wall(w,s,level,&found)<0 || check_plane(w,s,level,up,&found)<0) return -1;
        if (found && !(s->trans_done[s->tile]&bit)) {
            s->trans_done[s->tile]|=(uint8_t)bit;if (tlist(s,level)) return -1;
        }
    }
    adjacent=up ? bit<<1:bit>>1;
    if (((up ? s->above_open:s->below_open)&bit) && !(s->tile_seen[s->tile]&(adjacent<<s->level_shift)))
        return check_vertical(w,s,up ? level+1:level-1,up);
    return 0;
}

static int check_tile(const world *w,render_state *s)
{
    unsigned cur=s->tile,bit=s->level_bit,mask=bit<<s->level_shift;
    int found=0,wall=0;
    if (w->cells[0][cur].wall_sides==UINT32_MAX) {
        s->side_done[cur]|=(uint8_t)(1u<<s->side);s->budget=1;return 0;
    }
    if (!(s->tile_seen[cur]&mask)) {
        if (s->blocked&bit) s->tile_block[cur]|=mask;
        else {
            s->tile_seen[cur]|=mask;
            if (!(s->object_done[cur]&bit)) {
                found=check_objects(w,s,s->level);if (found<0) return -1;
                s->object_done[cur]|=(uint8_t)bit;
            }
            wall=check_wall(w,s,s->level,&found);if (wall<0) return -1;
            if (check_plane(w,s,s->level,0,&found) || check_plane(w,s,s->level,1,&found)) return -1;
            if (found && !(s->trans_done[cur]&bit)) {s->trans_done[cur]|=(uint8_t)bit;if (tlist(s,s->level)) return -1;}
            if (wall && !s->below_open && !s->above_open) {
                s->side_done[cur]|=(uint8_t)(1u<<s->side);s->budget=1;return 0;
            }
        }
    }
    if ((s->below_open&bit) && !(s->tile_seen[cur]&((bit>>1)<<s->level_shift)))
        if (check_vertical(w,s,s->level-1,0)) return -1;
    if ((s->above_open&bit) && !(s->tile_seen[cur]&((bit<<1)<<s->level_shift)))
        if (check_vertical(w,s,s->level+1,1)) return -1;
    s->blocked=(s->tile_block[cur]>>s->level_shift)&255;
    s->above_seen[cur]|=(uint8_t)s->above_open;s->below_seen[cur]|=(uint8_t)s->below_open;
    s->above_open=s->above_seen[cur];s->below_open=s->below_seen[cur];
    if (s->blocked&bit) {
        unsigned open=(s->below_open>>1)|(s->above_open<<1)|bit;
        unsigned budget=1;
        if ((s->blocked&open)==open) {s->budget=1;return 0;}
        if (s->above_open&bit) {
            unsigned depth=s->depth_above;
            if ((s->below_open&bit) && s->depth_below>depth) depth=s->depth_below;
            budget=depth*s->steps+depth+s->steps*4;
        } else if (s->below_open&bit) budget=s->depth_below*s->steps+s->depth_below+s->steps*4;
        if (budget<s->budget) s->budget=budget;
    }
    return 0;
}

static int ray(const world *w,render_state *s,unsigned column)
{
    uint16_t angle=(uint16_t)(s->camera.angle+s->ray_angles[column]);
    unsigned q=(angle&16383)>>3,quadrant=angle>>14;
    int sx=(quadrant==0 || quadrant==3) ? 1:-1,sy=quadrant<2 ? 1:-1;
    int32_t a=w->trig.tan[q],b=w->trig.tan[2047-q];
    int32_t dx=(quadrant&1) ? a:b,dy=(quadrant&1) ? b:a;
    int32_t cu=s->camera.u,cv=s->camera.v;
    int32_t fx=sx>0 ? 65535-(cu&65535):cu&65535;
    int32_t fy=sy>0 ? 65535-(cv&65535):cv&65535;
    int32_t x1=r_add(cu,r_mul(sx,r_mul16(fy,dx))),y1=r_add(cv,sy*fy);
    int32_t x2=r_add(cu,sx*fx),y2=r_add(cv,r_mul(sy,r_mul16(fx,dy)));
    int choose_y=sx>0 ? x1<=x2:x1>=x2;
    s->budget=R_RAY_MAX;s->steps=s->blocked=s->above_open=s->below_open=0;
    while (s->budget) {
        int32_t x=choose_y ? x1:x2,y=choose_y ? y1:y2;
        s->side=choose_y ? (sy>0 ? 1u:3u):(sx>0 ? 0u:2u);
        s->level_shift=s->side*8;
        s->tile=(((uint32_t)y>>16)<<5)|(((uint32_t)x>>16)&31);
        if (s->tile>=CM_MAP_CELLS) return r_fail(s,"ray outside map",s->tile);
        if (s->side_done[s->tile]&(1u<<s->side)) break;
        if (check_tile(w,s)) return -1;
        ++s->steps;if (!--s->budget) break;
        if (choose_y) {
            x1=r_clamp(r_add(x1,r_mul(sx,dx)),0,R_MAX_COORD);
            y1=r_clamp(r_add(y1,sy*R_FIXED_ONE),0,R_MAX_COORD);
            choose_y=sx>0 ? x1<=x2:x1>=x2;
        } else {
            x2=r_clamp(r_add(x2,sx*R_FIXED_ONE),0,R_MAX_COORD);
            y2=r_clamp(r_add(y2,r_mul(sy,dy)),0,R_MAX_COORD);
            choose_y=sy>0 ? y2>y1:y2<y1;
        }
    }
    return 0;
}

int r_build_lists(const world *w,render_state *s)
{
    int col;
    if (w->top_level<0 || w->top_level>=CM_MAP_LEVELS) return r_fail(s,"invalid top level",0);
    for (col=s->width/2-1;col>=0;--col) {
        s->order=(unsigned)col*2;
        if (ray(w,s,(unsigned)col)) return -1;
        ++s->order;
        if (ray(w,s,(unsigned)(s->width-col-1))) return -1;
    }
    return 0;
}
