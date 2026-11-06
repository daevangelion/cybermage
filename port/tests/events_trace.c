#include "events.h"
#include "ai.h"
#include "audio.h"
#include "replay.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

static world w;
static events_state e;
static sim_state s;
static ai_state a;

enum { EVENT_INPUT_WORDS=15 };
typedef struct { int64_t word[EVENT_INPUT_WORDS]; } event_input;

static int next_event_input(FILE *file, event_input *input)
{
    char line[1024],*p,*end;
    unsigned i;
    if(!fgets(line,sizeof(line),file)) return ferror(file)?-1:0;
    p=line;
    for(i=0;i<EVENT_INPUT_WORDS;++i) {
        errno=0;input->word[i]=strtoll(p,&end,10);
        if(errno || end==p || *end!=',' || input->word[i]<INT32_MIN ||
           input->word[i]>UINT32_MAX) return cm_fail("events-input.csv",i,"invalid input word");
        p=end+1;
    }
    if(input->word[0]<0 || input->word[1]<0 || input->word[2]<0 ||
       input->word[2]>=CM_OBJECT_MAX || input->word[3]<0 || input->word[3]>=CM_OBJECT_MAX ||
       input->word[8]<0 || input->word[8]>65535 || input->word[11]<1 || input->word[11]>11)
        return cm_fail("events-input.csv",0,"invalid input identity");
    if(strlen(p)!=257 || p[256]!='\n') return cm_fail("events-input.csv",0,"invalid key span");
    for(i=0;i<256;++i) if(!((p[i]>='0' && p[i]<='9') || (p[i]>='a' && p[i]<='f')))
        return cm_fail("events-input.csv",i,"invalid key byte");
    return 1;
}

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
    event_input input;
    FILE *input_file;
    char header[256];
    unsigned rows=0,checks=0,vehicle_rows=0,moves=0;
    int missing_world=0;
    int status,input_status;
    if(argc!=4) return 2;
    input_file=fopen(argv[3],"r");
    if(!input_file) return cm_fail(argv[3],0,"cannot open MoveWorld input observations");
    if(!fgets(header,sizeof(header),input_file) ||
       strcmp(header,"frame,ticks,object,person,x,y,z,object_z,heading,body,pitch,scene,mode,previous_mode,stance,keys\n"))
        return cm_fail(argv[3],0,"invalid MoveWorld input header");
    input_status=next_event_input(input_file,&input);
    if(input_status<0) return 1;
    sim_init(&s,0);
    if(snd_open(argv[1]) || replay_open(&replay,argv[2])) return 1;
    status=replay_next(&replay,&f);
    if(status<=0 || load_scene(argv[1],(unsigned)f.values[CM_TRACE_SCENE]) ||
       required_ranges(&replay)) return 1;
    snd_enable(0,0); /* Same original -x no-sound setting as the oracle. */
    a.events=&e;
    while(status>0) {
        unsigned r,i;
        if(missing_world) return cm_fail(argv[3],rows-1,"missing completed MoveWorld input frame");
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
        {
            int result=events_input(&e,&s,&a,&w);
            if(result<0) return 1;
            if(input_status>0 && input.word[0]<(int64_t)rows)
                return cm_fail(argv[3],rows,"stale or duplicate MoveWorld input frame");
            if(input_status>0 && input.word[0]==(int64_t)rows) {
                world_object *pc=&w.objects[w.player_object];
                if(result || w.player_object!=(unsigned)input.word[2] ||
                   w.player_person!=(unsigned)input.word[3] || w.scene!=(unsigned)input.word[11] ||
                   s.pc_mode!=(int32_t)input.word[12]) {
                    fprintf(stderr,"event input identity/mode divergence frame=%u computed=%u/%u/%d observed=%lld/%lld/%lld\n",
                        rows,w.player_object,w.player_person,s.pc_mode,(long long)input.word[2],
                        (long long)input.word[3],(long long)input.word[12]);
                    return 1;
                }
                /* Sim-owned state sampled before the original MoveWorld,
                 * never the next frame's post-event position. */
                pc->x=(int32_t)input.word[4];pc->y=(int32_t)input.word[5];pc->floor_z=(int32_t)input.word[6];
                pc->z=(int32_t)input.word[7];pc->heading=(uint16_t)input.word[8];
                s.body_height=(int32_t)input.word[9];w.camera.pitch=(int32_t)input.word[10];
                s.unk_144b34=(int32_t)input.word[14];s.timer_ticks=(uint32_t)input.word[1];
                if(events_move_world(&e,&s,&a,&w)) return 1;
                ++moves;
                input_status=next_event_input(input_file,&input);
                if(input_status<0) return 1;
            } else if(!result) missing_world=1;
        }
        ++rows;
        status=replay_next(&replay,&f);
    }
    fclose(input_file);replay_close(&replay); snd_close(); world_close(&w);
    if(status<0) return 1;
    if(!moves || input_status>0) return cm_fail(argv[3],rows,"empty or unused MoveWorld observations");
    printf("event reducer: %u frames %u independently computed state bytes matched; %u vehicle rows\n",rows,checks,vehicle_rows);
    return 0;
}
