#include "internal.h"
#include <string.h>

static float units(int32_t n) {return (float)n/65536.0f;}
static void vertex(glr_vertex *v,float x,float y,float z,float u,float t,unsigned light,unsigned mode)
{
    v->x=x;v->y=y;v->z=z;v->u=u;v->v=t;
    v->half_w=v->depth=v->bottom=v->top=0;v->light=light;v->mode=mode;
}

void glr_wall_quad(glr_vertex out[4],const world_object *o,const world_trig *trig,
                    int32_t half_columns,unsigned light,unsigned mode)
{
    unsigned i;
    float sin=(float)trig->sin[o->heading>>5]/65536,cos=(float)trig->cos[o->heading>>5]/65536;
    float bias=(float)((o->radius/half_columns)>>1);
    if (o->flags&UINT32_C(0x80000000)) mode|=GLR_MIRROR;
    for (i=0;i<4;++i) {
        float local=-(float)((int64_t)o->corner_x[i]-o->x)*sin+(float)((int64_t)o->corner_y[i]-o->y)*cos;
        float u=(local+(float)o->radius+bias)/((float)o->radius*2);
        vertex(out+i,units(o->corner_x[i]),units(o->corner_y[i]),
               units(o->z)+(i<2 ? -units(o->height):units(o->height)),
               u,i<2 ? 1.0f:0.0f,light,mode);
    }
}

void glr_flat_quad(glr_vertex out[4],const world_object *o,int top,unsigned light,unsigned mode)
{
    unsigned i;
    for (i=0;i<4;++i) {
        unsigned k=top ? 3-i:i;
        float x=units(o->x)+((k==0 || k==3) ? -units(o->radius):units(o->radius));
        float y=units(o->y)+(k<2 ? -units(o->height):units(o->height));
        vertex(out+i,x,y,units(o->z),y,x,light,mode|GLR_FLAT|((mode&GLR_MASK) ? (unsigned)(o->heading>>14)<<8:0));
    }
}

void glr_prop_quad(glr_vertex out[4],const world_object *o,const render_texture *t,
                   unsigned light,unsigned mode)
{
    unsigned i;
    for (i=0;i<4;++i) {
        glr_vertex *v=out+i;
        vertex(v,units(o->x),units(o->y),units(o->floor_z)+units(o->z),
               (i==0 || i==3) ? 1.0f:0.0f,i<2 ? 1.0f:0.0f,light,mode|GLR_BILLBOARD);
        v->half_w=units(t->frame_half_w);v->depth=units(t->frame_depth);
        v->bottom=-units(t->frame_base)-units(t->frame_height);
        v->top=(o->flags&UINT32_C(0x00001000) ? 0:units(t->frame_base))-units(t->frame_height);
    }
}

static void tile_object(world_object *o,unsigned tile,unsigned level,unsigned side,
                        unsigned plane,uint32_t flags)
{
    static const int32_t offsets[4][4]={
        {32768,-32768,-32768,32768},{-32768,32768,32768,-32768},
        {-32768,32768,32768,-32768},{32768,-32768,-32768,32768}};
    int32_t x=(int32_t)(tile%CM_MAP_SIDE)*65536,y=(int32_t)(tile/CM_MAP_SIDE)*65536;
    unsigned i;
    o->flags=flags;o->radius=o->height=32768;o->heading=(uint16_t)(side*16384);
    if (plane) {
        o->x=x+32768;o->y=y+32768;
        o->z=plane==2 ? (int32_t)(level+1)*65536-1:(int32_t)level*65536;
    } else {
        o->x=x+(side==0 ? 65535:side==2 ? 0:32768);
        o->y=y+(side==1 ? 65535:side==3 ? 0:32768);
        o->z=(int32_t)level*65536+32768;
        for (i=0;i<4;++i) {
            o->corner_x[i]=o->x+((side&1) ? offsets[side][i]:0);
            o->corner_y[i]=o->y+((side&1) ? 0:offsets[side][i]);
        }
    }
}

int glr_build_tiles(const world *w,glr_vertex *vertices,unsigned capacity,
                    glr_batch batches[GLR_TEXTURE_MAX],unsigned *quads)
{
    unsigned pass,level,tile,face,total=0,cursor[GLR_TEXTURE_MAX],i;
    memset(batches,0,sizeof *batches*GLR_TEXTURE_MAX);
    for (pass=0;pass<2;++pass) {
        for (level=0;level<CM_MAP_LEVELS;++level) for (tile=0;tile<CM_MAP_CELLS;++tile) {
            const world_cell *c=&w->cells[level][tile];
            if (w->cells[0][tile].wall_sides==UINT32_MAX) continue;
            for (face=0;face<6;++face) {
                unsigned plane=face<4 ? 0:face-3;
                unsigned id=plane ? (plane==2 ? c->ceiling:c->floor):(c->wall_sides>>(face*8))&255;
                unsigned texture,mode=0;
                uint32_t flags;
                if (!id) continue;
                if (c->light>=32) return cm_fail("GL mesh",tile,"light index outside table");
                flags=w->map_id_flags[id];texture=w->map_id_texture[id];
                if (texture>=w->texture_count || texture>=CM_RENDER_TEXTURES) {
                    if (flags&GLR_ORIG_TRANSPARENT) continue;
                    texture=CM_RENDER_TEXTURES;mode|=GLR_COLORLESS;
                }
                if (flags&GLR_ORIG_TRANSPARENT) {
                    mode|=(flags&GLR_ORIG_PACK) && !plane ? GLR_PACK:GLR_MASK;
                    if (plane==2) mode&=~(unsigned)GLR_MASK;
                }
                texture=texture*2+((mode&GLR_PACK) ? 1u:0u);
                if (!pass) {++batches[texture].count;++total;}
                else {
                    world_object o;
                    glr_vertex *out=vertices+cursor[texture]++*4;
                    tile_object(&o,tile,level,face%4,plane,flags);
                    if (plane) glr_flat_quad(out,&o,plane==2,c->light,mode);
                    else glr_wall_quad(out,&o,&w->trig,
                        texture/2==CM_RENDER_TEXTURES ? 1:w->textures[texture/2].height,c->light,mode);
                }
            }
        }
        if (!pass) {
            unsigned first=0;
            if (total>capacity) return cm_fail("GL mesh",total,"tile mesh pool full");
            for (i=0;i<GLR_TEXTURE_MAX;++i) {
                batches[i].first=cursor[i]=first;first+=batches[i].count;
            }
        }
    }
    *quads=total;return 0;
}
