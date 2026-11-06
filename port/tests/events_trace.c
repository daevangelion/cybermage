#include "events.h"
#include "ai.h"
#include "audio.h"
#include "replay.h"
#include <stdio.h>
#include <string.h>

static world w;
static events_state e;
static sim_state s;
static ai_state a;

static int covers(const cm_replay *replay, uint32_t address, uint32_t length)
{
    unsigned i;
    for(i=0;i<replay->range_count;++i) {
        const cm_trace_range *r=&replay->ranges[i];
        uint32_t offset;
        if(!r->relative || address<r->address) continue;
        offset=address-r->address;
        if(offset<=r->length && length<=r->length-offset) return 1;
    }
    return 0;
}

static int required_ranges(const cm_replay *replay)
{
    if(covers(replay,0x139390,e.event_count+1) &&
       covers(replay,0x139D90,(e.event_count+1)*64) &&
       covers(replay,0x142B78,e.switch_count)) return 0;
    return cm_fail("events_trace",0,"trace must contain all loaded event states, payloads and switch states");
}

static int load_scene(const char *data, unsigned scene)
{
    world_close(&w);
    if(world_load_scene(&w,data,scene) || ai_init(&a,&w) ||
       world_finalize_scene(&w) || sim_start_scene(&s,&w) ||
       events_init(&e,&w) || events_load_tables(&e,data)) return -1;
    a.events=&e;
    return 0;
}

static unsigned byte_at(const char *hex, unsigned i)
{
    unsigned n=0,j;
    for(j=0;j<2;++j) { unsigned c=(unsigned char)hex[i*2+j]; n=n*16+(c>='a'?c-'a'+10:c-'0'); }
    return n;
}

int main(int argc, char **argv)
{
    cm_replay replay={0};
    cm_replay_frame f;
    unsigned rows=0,checks=0,vehicle_rows=0;
    int status;
    if(argc!=3) return 2;
    sim_init(&s,0);
    if(snd_open(argv[1]) || replay_open(&replay,argv[2])) return 1;
    status=replay_next(&replay,&f);
    if(status<=0 || load_scene(argv[1],(unsigned)f.values[CM_TRACE_SCENE]) ||
       required_ranges(&replay)) return 1;
    snd_enable(0,0); /* Same original -x no-sound setting as the oracle. */
    a.events=&e;
    while(status>0) {
        unsigned r,i;
        if((unsigned)f.values[CM_TRACE_SCENE]!=w.scene) {
            unsigned scene=(unsigned)f.values[CM_TRACE_SCENE];
            if(!e.scene_requested.pending || e.scene_requested.scene!=(int32_t)scene) {
                fprintf(stderr,"event scene request divergence frame=%u observed=%u computed=%d pending=%d\n",
                    rows,scene,e.scene_requested.scene,e.scene_requested.pending);
                return 1;
            }
            if(load_scene(argv[1],scene) || required_ranges(&replay)) return 1;
        }
        /* Player/clock/key observations are reducer inputs. Event outputs never
         * get imported from the oracle. This is not integrated player parity. */
        if(w.player_object!=(unsigned)f.values[CM_TRACE_OBJECT] ||
           w.player_person!=(unsigned)f.values[CM_TRACE_PERSON]) {
            fprintf(stderr,"vehicle identity divergence frame=%u expected=%u/%u actual=%u/%u\n",
                rows,(unsigned)f.values[CM_TRACE_OBJECT],(unsigned)f.values[CM_TRACE_PERSON],w.player_object,w.player_person);
            return 1;
        }
        if(w.player_person) ++vehicle_rows;
        w.objects[w.player_object].x=(int32_t)f.values[CM_TRACE_X];
        w.objects[w.player_object].y=(int32_t)f.values[CM_TRACE_Y];
        w.objects[w.player_object].floor_z=(int32_t)f.values[CM_TRACE_Z];
        w.objects[w.player_object].heading=(uint16_t)f.values[CM_TRACE_HEADING];
        w.objects[w.player_object].z=(int32_t)f.values[CM_TRACE_OBJECT_Z];
        for(r=0;r<replay.range_count;++r) {
            const cm_trace_range *range=&replay.ranges[r];
            for(i=0;i<range->length;++i) {
                unsigned at=range->address+i,actual;
                if(!range->relative) {
                    if(at>=0x206820 && at<0x206824) actual=(uint32_t)e.tank_flag>>((at-0x206820)*8)&255;
                    else if(at>=0x206824 && at<0x206828) actual=(uint32_t)e.vehicle_type>>((at-0x206824)*8)&255;
                    else continue;
                } else if(at>=0x139190 && at<0x139390) actual=e.events[at-0x139190].type;
                else if(at>=0x139390 && at<0x139590) actual=e.events[at-0x139390].state;
                else if(at>=0x139590 && at<0x139790) actual=e.events[at-0x139590].enabled;
                else if(at>=0x139D90 && at<0x141D90) {
                    unsigned off=at-0x139D90;
                    actual=(uint32_t)e.events[off/64].value[(off%64)/4] >> (off%4*8) & 255;
                } else if(at>=0x142B78 && at<0x142BF8) actual=e.switches[at-0x142B78].state;
                else if(at>=0x14480C && at<0x144810) actual=(uint32_t)s.pc_mode>>((at-0x14480C)*8)&255;
                else continue;
                if(actual!=byte_at(f.range_hex[r],i)) {
                    fprintf(stderr,"event divergence frame=%u %s%x expected=%u actual=%u\n",
                        rows,range->relative?"Xox+":"link:",at,byte_at(f.range_hex[r],i),actual);
                    return 1;
                }
                ++checks;
            }
        }
        s.timer_ticks=(uint32_t)f.values[CM_TRACE_TICKS]; s.frame_ticks=(int32_t)f.values[CM_TRACE_DELTA];
        s.bp_limit_flag=(uint32_t)f.values[CM_TRACE_LIMIT_FLAG]; s.body_height=(int32_t)f.values[CM_TRACE_BODY];
        memcpy(s.kb_scan_status,f.input_keys,sizeof(s.kb_scan_status));
        if(events_step(&e,&s,&a,&w)) return 1;
        ++rows;
        status=replay_next(&replay,&f);
    }
    replay_close(&replay); snd_close(); world_close(&w);
    if(status<0) return 1;
    printf("event reducer: %u frames %u independently computed state bytes matched; %u vehicle rows\n",rows,checks,vehicle_rows);
    return 0;
}
