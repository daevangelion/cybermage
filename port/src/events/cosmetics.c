#include "events.h"
#include "ai.h"
#include "audio.h"

/* CM_HI.EXE LoadScene0116F9..011D46, MoveWorld037DCA..0387D9. */
void events_scene_cosmetics(events_state *e, const world *w)
{
    static const int32_t water[12][9]={
        {0},{44,45,46,27,28,29},{0},{0},{35,36,37},
        {55,56,57,56},{36,37,38},{29,30,31},
        {34,35,36,17,18,19,31,32,33},{60,61,62,63,64,65},
        {18,19,20,28,29},{31,32,33}
    };
    static const unsigned lengths[12]={0,6,0,0,3,4,3,3,9,6,5,3};
    unsigned i;
    for(i=0;i<lengths[w->scene];++i) e->waterNdx[i]=water[w->scene][i];
    for(i=0;i<w->prop_count;++i) e->prop_base_texture[i]=w->objects[w->prop_objects[i]].texture;
    e->DrakMangTime=0;e->unk_144B5C=0;
}

static int panel_alias(events_state *e, world *w, unsigned destination, unsigned source)
{
    int32_t dst=e->waterNdx[destination],src=e->waterNdx[source];
    world_texture *t;
    if((unsigned)dst>=w->panel_count || (unsigned)src>=w->panel_count)
        return cm_fail("MoveWorld/water",(uint32_t)src,"invalid animated panel");
    t=&w->textures[dst];
    if(t->pixels.data!=w->panels[src].pixels.data) {
        t->pixels=w->panels[src].pixels;t->pixel_owner=(unsigned)src+1;++t->revision;
    }
    return 0;
}

static int prop_frame(events_state *e, world *w, unsigned placement, unsigned frame)
{
    unsigned object,texture;
    if(placement>=w->prop_count) return cm_fail("MoveWorld/candle",placement,"missing prop placement");
    object=w->prop_objects[placement];texture=e->prop_base_texture[placement]+frame;
    if(object>=w->object_count || texture>=w->texture_count)
        return cm_fail("MoveWorld/candle",placement,"invalid prop animation frame");
    w->objects[object].texture=(uint16_t)texture;
    return 0;
}

int events_cosmetics(events_state *e, sim_state *s, ai_state *a, world *w)
{
    unsigned i,scene=w->scene;
    int due=(int32_t)e->ticks>(int32_t)e->waterTimer;
    if(scene==1 || scene==4 || scene==6 || scene==7 || scene==9 || scene==11) {
        if(due) {
            if((unsigned)e->waterFlag>=(scene==1?9u:12u))
                return cm_fail("MoveWorld/water",(uint32_t)e->waterFlag,"invalid water phase");
            if(panel_alias(e,w,0,(unsigned)e->waterFlag)) return -1;
            if(scene==1 && panel_alias(e,w,3,3+(unsigned)e->waterFlag)) return -1;
            e->waterFlag=(e->waterFlag+1)%3;e->waterTimer=e->ticks+(scene==6 || scene==7?4u:6u);
        }
    } else if(scene==5) {
        if(e->switches[3].state==1) {
            if(due) {
                if((unsigned)e->waterFlag>=12)
                    return cm_fail("MoveWorld/water",(uint32_t)e->waterFlag,"invalid water phase");
                if(panel_alias(e,w,0,(unsigned)e->waterFlag)) return -1;
                e->waterFlag=(e->waterFlag+1)%4;e->waterTimer=e->ticks+4;
            }
        } else if(panel_alias(e,w,0,0)) return -1;
    } else if(scene==8 || scene==10) {
        if(due) {
            /* Original jump tables037B14/037B24: 0,1,2,1. */
            if((unsigned)e->waterFlag>=4) return cm_fail("MoveWorld/water",0,"invalid water phase");
            unsigned phase=e->waterFlag==3?1u:(unsigned)e->waterFlag;
            if(panel_alias(e,w,0,phase)) return -1;
            if(scene==8 && (panel_alias(e,w,3,3+phase) || panel_alias(e,w,6,6+phase))) return -1;
            e->waterFlag=(e->waterFlag+1)%4;e->waterTimer=e->ticks+6;
        }
    }
    if((scene==9 || scene==10) && (int32_t)e->ticks>(int32_t)e->miscTimer) {
        unsigned count=scene==9?3:2;
        if((unsigned)e->miscFlag>=9)
            return cm_fail("MoveWorld/water",(uint32_t)e->miscFlag,"invalid secondary phase");
        if(panel_alias(e,w,3,3+(unsigned)e->miscFlag)) return -1;
        e->miscFlag=(e->miscFlag+1)%(int32_t)count;e->miscTimer=e->ticks+(scene==9?4u:2u);
        if(scene==9) {
            unsigned object;
            if(w->prop_count<=46) return cm_fail("MoveWorld/candle",46,"missing rotating prop");
            object=w->prop_objects[46];w->objects[object].heading=(uint16_t)(w->objects[object].heading-0x4000u);
        }
    }
    if(scene!=1 && scene!=7 && scene!=8 && scene!=10) return 0;
    e->candleflicker=(e->candleflicker+1)%(scene==1?3:scene==10?4:6);
    for(i=0;i<w->prop_count;++i) {
        unsigned frame=0,type=(unsigned)w->prop_types[i],object=w->prop_objects[i];
        int animate=0;
        if(scene==1) animate=type==147;
        else if(scene==7) animate=type==160;
        else if(scene==8) {
            if(type==165) w->objects[object].floor_z=0x8000+world_rnd(&w->rng,0x800);
            if(type==160) animate=1;
            else if(type==130) { animate=1;frame=(unsigned)e->candleflicker%2; }
            else if(type==169) { animate=1;frame=((unsigned)e->candleflicker%2)*14; }
            else if(type==168) {
                animate=1;
                if((i==109 && e->switches[52].state==1) || (i==110 && e->switches[53].state==1))
                    frame=1+(unsigned)e->candleflicker%3;
            }
        } else if(type==174) animate=1;
        else if(type==173) {
            animate=1;frame=(unsigned)e->candleflicker%2;
            if(a->gem_count_down && (int32_t)((uint32_t)a->gem_count_down_timer-10u)<(int32_t)e->ticks && world_rnd(&w->rng,2)) {
                ai_pc_set_shake(a,world_rnd(&w->rng,5)+1);(void)snd_play(27,32767,32768);
            }
            if(a->gem_count_down && (int32_t)e->ticks>a->gem_count_down_timer) {
                a->gem_count_down=0;(void)snd_play(27,32767,32768);
                world_omap_delete(w,object);w->objects[object].active=0;
                w->objects[object].x=w->objects[object].y=0x1F8000;ai_pc_set_shake(a,2);
            }
        }
        if(!animate) continue;
        if(type==147 || type==160 || type==174) frame=(unsigned)e->candleflicker;
        if(prop_frame(e,w,i,frame)) return -1;
    }
    if(scene==1 && prop_frame(e,w,86,(unsigned)e->candleflicker)) return -1;
    (void)s;return 0;
}
