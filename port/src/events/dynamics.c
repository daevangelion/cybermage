#include "events.h"
#include "ai.h"
#include "audio.h"
#include <limits.h>

/* CM_HI.EXE ReScaleProp036F10: scale the shared PROP resource, then shape z. */
int events_rescale_prop(world *w, unsigned placement, int32_t percent)
{
    unsigned type, object, i, resource;
    const uint8_t *record, *definition;
    int32_t scale;
    int64_t value;
    if (placement >= w->prop_count || percent < 0)
        return cm_fail("ReScaleProp",placement,"invalid prop scale request");
    type=(unsigned)w->prop_types[placement]; object=w->prop_objects[placement];
    if (type >= w->prop_definitions.size/1051 || object >= w->object_count ||
        w->scene_sections[1].size < (placement+2)*30u)
        return cm_fail("ReScaleProp",placement,"invalid prop definition");
    record=w->scene_sections[1].data+(placement+1)*30u;
    definition=w->prop_definitions.data+type*1051u;
    value=(int64_t)cm_u16(definition+30)*percent/100;
    if(value>INT32_MAX/512) return cm_fail("ReScaleProp",placement,"scale overflow");
    scale=(int32_t)value*512/100;
    resource=w->textures[w->objects[object].texture].resource;
    for(i=0;i<w->texture_count;++i) {
        world_texture *t=&w->textures[i];
        if(t->kind!=WORLD_TEX_PROP || t->resource!=resource) continue;
        if((int64_t)t->height*scale>INT32_MAX || (int64_t)t->width*scale>INT32_MAX)
            return cm_fail("ReScaleProp",placement,"texture extent overflow");
        t->half_w=t->height*scale; t->base=t->width*scale;
    }
    value=(int64_t)(int32_t)cm_u32(definition+8)*percent/100;
    if(value<INT32_MIN || value>INT32_MAX) return cm_fail("ReScaleProp",placement,"shape extent overflow");
    w->objects[object].z=(int32_t)((uint32_t)cm_u32(record+8)+(uint32_t)value);
    return 0;
}

/* ZFlatDropEm036FE8 and ZFlatRaiseEm0370EC include rectangle boundaries. */
static int inside(const world_object *p, const world_object *flat)
{
    return (int64_t)p->x >= (int64_t)flat->x-flat->radius &&
        (int64_t)p->x <= (int64_t)flat->x+flat->radius &&
        (int64_t)p->y >= (int64_t)flat->y-flat->height &&
        (int64_t)p->y <= (int64_t)flat->y+flat->height;
}
void events_zflat_drop(world *w, unsigned object, int32_t previous_z)
{
    const world_object *flat=&w->objects[object];
    unsigned i;
    for(i=0;i<w->person_count;++i) {
        int index=w->persons[i].object;
        world_object *p;
        int32_t z;
        if(index<0 || (unsigned)index>=w->object_count) continue;
        p=&w->objects[index];
        if(!inside(p,flat) || p->floor_z<flat->z || p->floor_z>previous_z) continue;
        z=flat->z<0?0:flat->z;
        if(z<p->floor_z-0x4000) z=p->floor_z-0x4000;
        p->floor_z=z;
    }
}
void events_zflat_raise(world *w, unsigned object, int32_t previous_z)
{
    const world_object *flat=&w->objects[object];
    unsigned i;
    for(i=0;i<w->person_count;++i) {
        int index=w->persons[i].object;
        world_object *p;
        if(index<0 || (unsigned)index>=w->object_count) continue;
        p=&w->objects[index];
        if(inside(p,flat) && previous_z<=p->floor_z && p->floor_z<=flat->z) p->floor_z=flat->z;
    }
}

static int crush_damage(sim_state *s, ai_state *a, world *w, unsigned person, int wall)
{
    int32_t damage;
    if(person==w->player_person) {
        damage=world_rnd(&w->rng,wall?3:8)+(wall?1:4);
        if(ai_damage_person(a,w,person,damage,100,50)) return -1;
        if(a->pc_sound<3) a->pc_sound=3;
    } else {
        damage=world_rnd(&w->rng,wall?4:8)+(wall?2:4);
        if(ai_damage_monster(a,w,person,damage,-1)) return -1;
    }
    (void)s;
    return 0;
}

/* ZWallDCrash037474: only cardinal panels are valid. Wall bounds are strict. */
int events_zwall_dcrash(sim_state *s, ai_state *a, world *w, unsigned object)
{
    const world_object *wall=&w->objects[object];
    int32_t dx=0x3800,dy=0x3800;
    unsigned i;
    int hit=0;
    if(wall->heading==0x4000 || wall->heading==0xC000) dx+=wall->radius;
    else if(!wall->heading || wall->heading==0x8000) dy+=wall->radius;
    else return cm_fail("ZWallDCrash",object,"non-cardinal crushing wall");
    for(i=0;i<w->person_count;++i) {
        int index=w->persons[i].object;
        const world_object *p;
        int64_t head,shoulder,lower=(int64_t)wall->z-wall->height;
        if(w->persons[i].status==2 || index<0 || (unsigned)index>=w->object_count) continue;
        p=&w->objects[index];
        if((int64_t)p->x<=wall->x-dx || (int64_t)p->x>=wall->x+dx ||
           (int64_t)p->y<=wall->y-dy || (int64_t)p->y>=wall->y+dy) continue;
        shoulder=(int64_t)p->floor_z+p->z; head=shoulder+p->height;
        if(i==w->player_person) head+=s->unk_144b34;
        if(lower>=head || lower<=shoulder) continue;
        if(crush_damage(s,a,w,i,1)) return -1;
        hit=1;
    }
    if(hit) (void)snd_object(w,5,object,0x60000,40959,0);
    return hit;
}

/* ZFlatDCrash0376FC: the descending plane must cut the actor's vertical span. */
int events_zflat_dcrash(sim_state *s, ai_state *a, world *w, unsigned object)
{
    const world_object *flat=&w->objects[object];
    unsigned i;
    int hit=0;
    for(i=0;i<w->person_count;++i) {
        int index=w->persons[i].object;
        const world_object *p;
        int64_t head;
        if(w->persons[i].status==2 || index<0 || (unsigned)index>=w->object_count) continue;
        p=&w->objects[index]; head=(int64_t)p->floor_z+p->z+p->height;
        if(i==w->player_person) head+=s->unk_144b34;
        if(!inside(p,flat) || flat->z<=p->floor_z || head<=flat->z) continue;
        if(crush_damage(s,a,w,i,0)) return -1;
        if(i==w->player_person && (s->pc_mode==1 || s->pc_mode==2 || s->pc_mode==5)) s->unk_144b3c=-1;
        hit=1;
    }
    if(hit) (void)snd_object(w,5,object,0x60000,40959,0);
    return hit;
}

/* ZFlatUCrash0378F8 searches ceiling tiles above the old plane, not camera z. */
int events_zflat_ucrash(sim_state *s, ai_state *a, world *w, unsigned object, int32_t previous_z)
{
    const world_object *flat=&w->objects[object];
    unsigned i;
    int level=previous_z>>16,tile=(flat->x>>16)+(flat->y>>16)*CM_MAP_SIDE;
    int64_t ceiling;
    int hit=0;
    if(tile<0 || tile>=CM_MAP_CELLS || level<0 || level>=CM_MAP_LEVELS)
        return cm_fail("ZFlatUCrash",object,"flat outside map");
    while(level<w->top_level && !w->cells[level][tile].ceiling) ++level;
    ceiling=(int64_t)level*65536+65535;
    for(i=0;i<w->person_count;++i) {
        int index=w->persons[i].object;
        const world_object *p;
        if(w->persons[i].status==2 || index<0 || (unsigned)index>=w->object_count) continue;
        p=&w->objects[index];
        if(!inside(p,flat) || p->floor_z<flat->z || p->floor_z>=ceiling ||
            (int64_t)p->floor_z+p->z+p->height<=ceiling) continue;
        if(crush_damage(s,a,w,i,0)) return -1;
        hit=1;
    }
    if(hit) (void)snd_object(w,5,object,0x60000,40959,0);
    return hit;
}

/* YCrushCrash0371C8 pushes sideways first; damage only when trapped between slabs. */
int events_ycrush_crash(sim_state *s, ai_state *a, world *w, unsigned object, unsigned other)
{
    const world_object *wall=&w->objects[object],*back=&w->objects[other];
    unsigned i;
    int hit=0;
    int32_t minx=wall->x-wall->radius-0x3800,maxx=wall->x+wall->radius+0x3800;
    int32_t miny=wall->y-0x3800,maxy=wall->y+0x3800;
    int32_t limit=back->y<wall->y?back->y+0x3800:back->y-0x3800;
    for(i=0;i<w->person_count;++i) {
        int index=w->persons[i].object,trapped=0;
        world_object *p;
        if(index<0 || (unsigned)index>=w->object_count) continue;
        p=&w->objects[index];
        if(p->x<=minx || p->x>=maxx || p->y<=miny || p->y>=maxy ||
           (int64_t)wall->z+wall->height<=p->floor_z ||
           (int64_t)p->floor_z+p->z+p->height<=(int64_t)wall->z-wall->height) continue;
        if(p->x<=wall->x-wall->radius) p->x=minx;
        else if(p->x>=wall->x+wall->radius) p->x=maxx;
        else if(p->y<wall->y) { if(miny<limit) trapped=1; else p->y=miny; }
        else if(p->y>wall->y) { if(maxy>limit) trapped=1; else p->y=maxy; }
        if(!trapped || w->persons[i].status==2) continue;
        if(crush_damage(s,a,w,i,0)) return -1;
        hit=1;
    }
    if(hit) (void)snd_object(w,5,object,0x60000,40959,0);
    return hit;
}
