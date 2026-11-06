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
static ui_save_state saved;
static cm_ui names;

enum { EVENT_INPUT_WORDS=15 };
typedef struct { int64_t word[EVENT_INPUT_WORDS]; } event_input;

typedef struct { uint32_t frame,ticks,type,object,source; } item_input;
typedef struct {
    uint32_t frame,ticks,action;
    int mouse,joystick;
    int32_t hand_x,hand_y;
} selector_input;
typedef struct { FILE *file; uint32_t frame; int seen; } player_reader;
typedef struct { uint32_t frame; int16_t hp; } player_input;

static int read_words(FILE *file,int64_t *words,unsigned count,const char *path)
{
    char line[256],*p,*end;
    unsigned i;
    if(!fgets(line,sizeof line,file)) return ferror(file)?cm_fail(path,0,"cannot read input row"):0;
    p=line;
    for(i=0;i<count;++i) {
        errno=0;words[i]=strtoll(p,&end,10);
        if(errno || end==p || words[i]<INT32_MIN || words[i]>UINT32_MAX ||
           *end!=(i+1==count?'\n':',') || (i+1==count && end[1]))
            return cm_fail(path,i,"invalid numeric input word");
        p=end+1;
    }
    return 1;
}

static int next_item_input(FILE *file,item_input *input,const char *path)
{
    int64_t word[5];
    int result=read_words(file,word,5,path);
    unsigned i;
    if(result<=0) return result;
    for(i=0;i<5;++i) if(word[i]<0) return cm_fail(path,i,"negative item input word");
    if(!word[2] || word[2]>255 || word[3]>=CM_OBJECT_MAX || word[4]>=CM_OBJECT_MAX)
        return cm_fail(path,2,"invalid item input identity");
    input->frame=(uint32_t)word[0];input->ticks=(uint32_t)word[1];
    input->type=(uint32_t)word[2];input->object=(uint32_t)word[3];input->source=(uint32_t)word[4];
    return 1;
}

static int next_player_input(player_reader *reader,player_input *input,const char *path)
{
    int64_t word[18];
    int result;
    while((result=read_words(reader->file,word,18,path))>0) {
        if(word[0]<0 || word[1]<0 || word[5]<0 || word[5]>=CM_OBJECT_MAX ||
           word[6]<0 || word[6]>=CM_OBJECT_MAX || word[13]<INT16_MIN || word[13]>INT16_MAX)
            return cm_fail(path,0,"invalid actor input identity or HP");
        if(reader->seen && (uint32_t)word[0]<reader->frame)
            return cm_fail(path,(uint32_t)word[0],"reordered actor frame");
        if(!reader->seen || (uint32_t)word[0]!=reader->frame) {
            if(word[5]) return cm_fail(path,(uint32_t)word[0],"actor frame lacks leading PERSON0");
            reader->frame=(uint32_t)word[0];reader->seen=1;
        }
        if(!word[5]) {
            input->frame=(uint32_t)word[0];input->hp=(int16_t)word[13];
            return 1;
        }
    }
    return result;
}

static int next_selector_input(FILE *file,selector_input *input,const char *path)
{
    int64_t word[7],x,y;
    unsigned i;
    int result=read_words(file,word,7,path);
    if(result<=0) return result;
    if(word[0]<0 || word[1]<0 || word[2]<0 || word[2]>2 ||
       word[5]>INT32_MAX || word[6]>INT32_MAX)
        return cm_fail(path,0,"invalid selector input identity, action or device");
    if(word[2]) {
        for(i=3;i<7;++i) if(word[i])
            return cm_fail(path,i,"UI selector reset has unexpected pointer inputs");
    }
    if(!word[3] && word[4] && (word[5] || word[6]))
        return cm_fail(path,5,"nonzero joystick hand observations are not supported");
    x=word[5]*2+320;y=(word[6]+100)*2;
    if(x<INT32_MIN || x>INT32_MAX || y<INT32_MIN || y>INT32_MAX)
        return cm_fail(path,5,"selector hand coordinates exceed native input bounds");
    input->frame=(uint32_t)word[0];input->ticks=(uint32_t)word[1];
    input->action=(uint32_t)word[2];input->mouse=word[3]!=0;input->joystick=word[4]!=0;
    input->hand_x=(int32_t)word[5];input->hand_y=(int32_t)word[6];
    return 1;
}

static int apply_selector_input(const selector_input *input,const char *path)
{
    if(input->action) events_reset_selectors(&e);
    else {
        cm_input pointer={0};
        if((input->mouse && s.input_device!=1) || (input->joystick && s.input_device!=2))
            return cm_fail(path,input->frame,"selector device disagrees with original user configuration");
        pointer.mouse_present=input->mouse;pointer.joystick_present=input->joystick;
        pointer.mouse_x=(int)((int64_t)input->hand_x*2+320);
        pointer.mouse_y=(int)(((int64_t)input->hand_y+100)*2);
        sim_input(&s,&pointer);
    }
    return 0;
}

/* Only CollObject's external calls (returns043898/043B0A/043D7E/043FF0)
 * are observed; event-owned grants must not be injected a second time.
 * Native item/event outputs still come exclusively from the reducer. */
static int apply_item_input(const item_input *input,const char *path)
{
    unsigned i;
    if(input->object>=w.object_count || input->source>=w.object_count ||
       input->type>e.definition_count)
        return cm_fail(path,input->frame,"item input exceeds loaded scene");
    if(input->source!=w.player_object)
        return cm_fail(path,input->frame,"observed CollItem source is not the player");
    for(i=0;i<w.prop_count;++i) if(w.prop_objects[i]==input->object) {
        if((unsigned)(uint16_t)w.prop_items[i]!=input->type)
            return cm_fail(path,input->frame,"item input type differs from loaded PROP auxiliary");
        e.ticks=input->ticks;
        return events_coll_item(&e,&w,input->object,input->source);
    }
    return cm_fail(path,input->frame,"item input object has no PROP placement");
}

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

static int regen_timer_range(const cm_replay *replay)
{
    unsigned i;
    for(i=0;i<replay->range_count;++i) {
        const cm_trace_range *r=&replay->ranges[i];
        if(!r->relative && r->address<=0x206834 &&
           (uint64_t)r->address+r->length>=0x206838) return 1;
    }
    return 0;
}

static int inventory_ranges(const cm_replay *replay)
{
    unsigned i;
    for(i=0;i<replay->range_count;++i) {
        const cm_trace_range *r=&replay->ranges[i];
        if(r->relative &&
           ((r->address<0x131418 && (uint64_t)r->address+r->length>0x130D64) ||
            (r->address<0x1449CC && (uint64_t)r->address+r->length>0x144974))) return 1;
    }
    return 0;
}

static int inventory_byte(uint32_t at,unsigned *actual)
{
    uint32_t value,off;
    if(at>=0x130D64 && at<0x1313A4) {
        const events_item *v;
        off=at-0x130D64;v=&e.items[off/40];off%=40;
        if(off<20) *actual=(unsigned char)v->name[off];
        else if(off<24) {
            value=(uint16_t)(off<22?v->type:v->object);*actual=value>>(off%2*8)&255;
        } else if(off<32) {
            value=(uint32_t)(off<28?v->icon:v->quantity);*actual=value>>(off%4*8)&255;
        } else if(off==32) *actual=v->kind;
        else if(off==33) *actual=v->subtype;
        else if(off==34) *actual=v->flags;
        else if(off==35) *actual=v->variant;
        else *actual=(uint32_t)v->unk_0024>>(off%4*8)&255;
        return 1;
    }
    if(at>=0x1313A4 && at<0x1313C0) {
        off=at-0x1313A4;
        switch(off/4) {
        case 0:value=(uint32_t)e.cash;break;
        case 1:value=(uint32_t)e.spell;break;
        case 2:value=(uint32_t)e.hh_weapon;break;
        case 3:value=(uint32_t)e.weapon;break;
        case 4:value=(uint32_t)e.item_spell;break;
        case 5:value=e.weapon_slot;break;
        default:value=e.hh_dice;break;
        }
        *actual=value>>(off%4*8)&255;return 1;
    }
    if(at>=0x1313C0 && at<0x1313C8) {*actual=e.spells[at-0x1313C0];return 1;}
    if(at>=0x1313C8 && at<0x131414) {
        off=at-0x1313C8;
        if(off<12) value=(uint16_t)e.armor[off/2];
        else if(off<24) value=(uint16_t)e.max_armor[(off-12)/2];
        else if(off<48) value=(uint16_t)e.ammo[(off-24)/2];
        else if(off<72) value=(uint16_t)e.max_ammo[(off-48)/2];
        else value=(uint16_t)(off<74?e.energy:e.max_energy);
        *actual=value>>(off%2*8)&255;return 1;
    }
    if(at>=0x131414 && at<0x131418) {
        *actual=(uint32_t)a.pc.regen_interval>>((at-0x131414)*8)&255;return 1;
    }
    if(at>=0x144974 && at<0x1449CC) {
        off=at-0x144974;
        if(off<4) value=e.selectors[EVENTS_SELECT_KEY-1].count;
        else if(off<8) value=e.selectors[EVENTS_SELECT_KEY-1].index;
        else value=(uint32_t)e.selector_keys[(off-8)/4];
        *actual=value>>(off%4*8)&255;return 1;
    }
    return 0;
}

static int load_scene(const char *data, unsigned scene)
{
    uint32_t regen_timer=a.pc.sp_regen_timer;
    world_close(&w);
    s.input_device=names.config.in_device;
    if(world_load_scene(&w,data,scene) ||
       ai_init(&a,&w,(unsigned)names.config.difficulty)) return -1;
    a.pc.sp_regen_timer=regen_timer;
    if(ai_set_difficulty(&a,&w,(unsigned)names.config.difficulty) ||
       ai_load_tables(&a,data) ||
       world_finalize_scene(&w) || sim_start_scene(&s,&w) ||
       events_init(&e,&w) || events_load_tables(&e,data)) return -1;
    a.events=&e;
    return 0;
}

static int user_file(cm_file *file, const char *user, const char *name)
{
    char path[1024];
    int n=snprintf(path,sizeof path,"%s/%s",user,name);
    if(n<0 || (size_t)n>=sizeof path) return cm_fail(user,0,"save input path too long");
    return platform_file_read(file,path);
}

/* Initialize from original-written SAVE bytes, never a compared event span. */
static int load_saved_scene(const char *data,const char *user,unsigned slot,
                             uint16_t seed,uint32_t ticks)
{
    cm_file file={0},executable={0},metadata={0};
    cm_exe exe;
    char name[24];
    uint32_t fire[4];
    unsigned scene;
    int result=-1,n;
    n=snprintf(name,sizeof name,"SAVE%03u.DAT",slot);
    if(n<0 || (size_t)n>=sizeof name) return cm_fail("SAVE*.DAT",slot,"invalid save name");
    if(user_file(&file,user,name) || ui_save_scene(file.data,file.size,&scene) ||
       user_file(&metadata,user,"GAMENAME.USR") ||
       ui_game_names_read(&names,metadata.data,metadata.size) ||
       user_file(&executable,data,"CM_HI.EXE") ||
       cm_exe_init(&exe,&executable,"CM_HI.EXE")) goto out;
    world_rng_init(&w.rng,seed);
    sim_init(&s,ticks);
    s.ai=&a;s.events=&e;
    if(ai_init_pc(&e,&s,&w.rng,&exe,1,0) || load_scene(data,scene) ||
       ui_save_overlay(&saved,&w,file.data,file.size) ||
       sim_save_import(&s,saved.unk_6D4)) goto out;
    fire[0]=saved.FireCheck;fire[1]=saved.FireTilePanel;
    fire[2]=saved.FireTileArt;fire[3]=saved.FireDamage;
    e.ticks=ticks;ai_start_clock(&a,ticks);
    if(sim_save_fire_import(&s,fire) || ai_save_import(&a,&w,&saved) ||
       events_save_import(&e,&saved) || events_text_restore(&e,&w) ||
       ui_text_restore(&names,&saved,e.text_serial)) goto out;
    a.difficulty=1; /* Original SAVE stores normalized PERSON difficulty. */
    if(ai_set_difficulty(&a,&w,(unsigned)names.config.difficulty)) goto out;
    s.input_device=names.config.in_device;
    memcpy(e.auto_ptr,names.auto_ptr,sizeof e.auto_ptr);
    memcpy(e.auto_slot,names.auto_slot,sizeof e.auto_slot);
    ai_save_pc_info(&e,&w);
    result=0;
out:
    platform_file_release(&metadata);
    platform_file_release(&executable);
    platform_file_release(&file);
    return result;
}

static unsigned byte_at(const char *hex, unsigned i)
{
    unsigned n=0,j;
    for(j=0;j<2;++j) { unsigned c=(unsigned char)hex[i*2+j]; n=n*16+(c>='a'?c-'a'+10:c-'0'); }
    return n;
}

static int draw_clock(void *context,uint32_t site,uint32_t *ticks)
{
    return replay_draw_clock(context,site,ticks);
}

static int draw_state(cm_replay *replay,uint32_t frame,unsigned phase)
{
    const cm_draw_clock clock={replay,draw_clock};
    ui_hud hud;
    int result=replay_draw_begin(replay,frame,phase);
    if(result<=0) return result;
    /* Pixel-free composition of the actual StatusMonitor and PC draw-state
     * producers. Their measured reads are inputs, never expected state. */
    if(ui_hud_from_world(&hud,&w,&e,names.hp_count,s.pc_mode) ||
       (ui_status_visible(&names,&hud) && ui_status_advance(&names,&clock)) ||
       ai_pc_draw_advance(&a,&s,&w,&clock) || replay_draw_end(replay)) return -1;
    return 1;
}

int main(int argc, char **argv)
{
    cm_replay replay={0};
    cm_replay_frame f;
    event_input input;
    item_input item={0};
    selector_input selector={0};
    player_input player={0};
    player_reader actor_reader={0};
    FILE *input_file,*item_file=NULL,*selector_file=NULL;
    char header[256];
    unsigned rows=0,checks=0,vehicle_rows=0,moves=0;
    unsigned item_calls=0,hp_checks=0,inventory_checks=0,selector_checks=0;
    unsigned sim_pointer_bytes=0,unverified_pc_globals=0;
    unsigned selector_calls=0,selector_resets=0;
    unsigned game_draws=0,save_draws=0;
    int missing_world=0;
    int status,input_status,item_status=0,player_status=0,selector_status=0,load_slot=-1,arg;
    int check_inventory;
    const char *user=NULL,*item_path=NULL,*actor_path=NULL,*selector_path=NULL;
    if(argc<4) return 2;
    for(arg=4;arg<argc;++arg) {
        if(!strcmp(argv[arg],"--user") && arg+1<argc && !user) user=argv[++arg];
        else if(!strcmp(argv[arg],"--item-input") && arg+1<argc && !item_path) item_path=argv[++arg];
        else if(!strcmp(argv[arg],"--actors") && arg+1<argc && !actor_path) actor_path=argv[++arg];
        else if(!strcmp(argv[arg],"--selector-input") && arg+1<argc && !selector_path) selector_path=argv[++arg];
        else if(!strcmp(argv[arg],"--replay-load-slot") && arg+1<argc && load_slot<0) {
            unsigned long value;char *end;const char *text=argv[++arg];
            if(!*text || *text=='-') return 2;
            errno=0;value=strtoul(text,&end,10);
            if(errno || *end || value>UI_SAVE_SLOTS) return 2;
            load_slot=(int)value;
        } else return 2;
    }
    if((load_slot>=0)!=(user!=NULL)) return 2;
    if((item_path!=NULL)!=(actor_path!=NULL)) return 2;
    if((item_path!=NULL)!=(selector_path!=NULL)) return 2;
    ui_config_defaults(&names.config);
    if(user) {
        char path[1024];
        int n=snprintf(path,sizeof path,"%s/CMDATA.CNF",user);
        if(n<0 || (size_t)n>=sizeof path) return cm_fail(user,0,"configuration path too long");
        if(ui_config_load(&names.config,path)) return 1;
    }
    ui_options_from_config(&names);
    names.vdu=1; /* Same initial monitor selection as ui_init; not a CNF field. */
    input_file=fopen(argv[3],"r");
    if(!input_file) return cm_fail(argv[3],0,"cannot open MoveWorld input observations");
    if(!fgets(header,sizeof(header),input_file) ||
       strcmp(header,"frame,ticks,object,person,x,y,z,object_z,heading,body,pitch,scene,mode,previous_mode,stance,keys\n"))
        return cm_fail(argv[3],0,"invalid MoveWorld input header");
    input_status=next_event_input(input_file,&input);
    if(input_status<0) return 1;
    if(item_path) {
        item_file=fopen(item_path,"r");
        if(!item_file) return cm_fail(item_path,0,"cannot open external CollItem observations");
        if(!fgets(header,sizeof header,item_file) ||
           strcmp(header,"frame,ticks,type,object,source\n"))
            return cm_fail(item_path,0,"invalid external CollItem header");
        item_status=next_item_input(item_file,&item,item_path);
        actor_reader.file=fopen(actor_path,"r");
        if(!actor_reader.file) return cm_fail(actor_path,0,"cannot open actor HP comparisons");
        if(!fgets(header,sizeof header,actor_reader.file) ||
           strcmp(header,"frame,ticks,rng0,rng1,rng2,person,object,x,y,z,heading,state,status,hp,target,animation,cursor,active\n"))
            return cm_fail(actor_path,0,"invalid actor comparison header");
        player_status=next_player_input(&actor_reader,&player,actor_path);
        selector_file=fopen(selector_path,"r");
        if(!selector_file) return cm_fail(selector_path,0,"cannot open selector input observations");
        if(!fgets(header,sizeof header,selector_file) ||
           strcmp(header,"frame,ticks,action,mouse,joystick,hand_x,hand_y\n"))
            return cm_fail(selector_path,0,"invalid selector input header");
        selector_status=next_selector_input(selector_file,&selector,selector_path);
        if(item_status<0 || player_status<0 || selector_status<0) return 1;
    }
    sim_init(&s,0);
    s.ai=&a;s.events=&e;
    if(snd_open(argv[1]) || replay_open(&replay,argv[2])) return 1;
    status=replay_next(&replay,&f);
    if(status<=0) return 1;
    if(load_slot>=0) {
        if(load_saved_scene(argv[1],user,(unsigned)load_slot,
                            replay.has_bios_seed?replay.bios_seed:0,
                            (uint32_t)f.values[CM_TRACE_PREVIOUS])) return 1;
        if(w.scene!=(unsigned)f.values[CM_TRACE_SCENE])
            return cm_fail(argv[2],0,"loaded save scene differs from first trace record");
    } else if(load_scene(argv[1],(unsigned)f.values[CM_TRACE_SCENE])) return 1;
    if(required_ranges(&replay)) return 1;
    check_inventory=inventory_ranges(&replay) || item_path;
    if(check_inventory &&
       (!item_path || !covers(&replay,0x130D64,1600) ||
        !covers(&replay,0x1313A4,116) || !covers(&replay,0x144974,88) ||
        !regen_timer_range(&replay)))
        return cm_fail(argv[2],0,"inventory verification requires item/actor observations and complete inventory/equipment/selector ranges");
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
        if(actor_path) {
            int actual;
            if(player_status<=0 || player.frame!=rows || !w.person_count)
                return cm_fail(actor_path,rows,"missing, stale or duplicate PERSON0 HP comparison");
            actual=(int16_t)(uint16_t)w.persons[0].unk_0038;
            if(actual!=player.hp) {
                fprintf(stderr,"actor0 HP divergence frame=%u expected=%d actual=%d\n",rows,player.hp,actual);
                return 1;
            }
            ++hp_checks;
        }
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
                    else if(at>=0x206824 && at<0x206828) actual=(uint32_t)s.tank_hand>>((at-0x206824)*8)&255;
                    else if(at>=0x206834 && at<0x206838) actual=a.pc.sp_regen_timer>>((at-0x206834)*8)&255;
                    else if(at>=0x206828 && at<0x206834) {++unverified_pc_globals;continue;}
                    else continue;
                } else if(inventory_byte(at,&actual)) {
                    if(at>=0x144974) ++selector_checks;
                    else ++inventory_checks;
                } else if(at>=0x131418 && at<0x131424) {
                    /* Sim flight height and two guest PERSON pointers
                     * are outside this Events/shared-PC output gate. */
                    ++sim_pointer_bytes;continue;
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
        if(f.input_keys[0x19])
            return cm_fail(argv[2],rows,"dedicated P/pause deadline adjustments require the full game loop");
        s.timer_ticks=(uint32_t)f.values[CM_TRACE_TICKS]; s.frame_ticks=(int32_t)f.values[CM_TRACE_DELTA];
        s.bp_limit_flag=(uint32_t)f.values[CM_TRACE_LIMIT_FLAG]; s.body_height=(int32_t)f.values[CM_TRACE_BODY];
        memcpy(s.kb_scan_status,f.input_keys,sizeof(s.kb_scan_status));
        {
            int result;
            if(check_inventory) {
                cm_replay_ui action;
                while((result=replay_ui_next(&replay,rows,&action))>0) {
                    if(action.action!=1)
                        return cm_fail(replay.ui_action_path,rows,"mid-trace Load is outside the isolated inventory reducer");
                    result=draw_state(&replay,rows,CM_DRAW_SAVE);
                    if(result<0) return 1;
                    if(result>0) ++save_draws;
                }
                if(result<0) return 1;
            }
            if(selector_status>0 && selector.frame<rows)
                return cm_fail(selector_path,rows,"stale selector input frame");
            while(selector_status>0 && selector.frame==rows && selector.action) {
                if(apply_selector_input(&selector,selector_path)) return 1;
                ++selector_resets;
                selector_status=next_selector_input(selector_file,&selector,selector_path);
                if(selector_status<0) return 1;
            }
            if(events_frame_begin(&e,&s,&a,&w)) return 1;
            if(events_keyboard(&e,&s,&a,&w)) return 1;
            if(events_selector_needs_input(&e)) {
                if(selector_status<=0 || selector.frame!=rows || selector.action)
                    return cm_fail(selector_path?selector_path:argv[2],rows,"missing ProcessSelector input frame");
                if(input_status>0 && input.word[0]==(int64_t)rows) {
                    uint32_t start=(uint32_t)f.values[CM_TRACE_TICKS];
                    uint32_t span=(uint32_t)input.word[1]-start;
                    if(span>INT32_MAX || selector.ticks-start>span)
                        return cm_fail(selector_path,rows,"selector observation is outside the pre-MoveWorld phase");
                }
                if(apply_selector_input(&selector,selector_path)) return 1;
                ++selector_calls;
                selector_status=next_selector_input(selector_file,&selector,selector_path);
                if(selector_status<0) return 1;
            }
            if(selector_status>0 && selector.frame==rows)
                return cm_fail(selector_path,rows,"unexpected, duplicate or late selector input action");
            result=events_input(&e,&s,&a,&w);
            if(result<0) return 1;
            if(item_status>0 && item.frame<rows)
                return cm_fail(item_path,rows,"stale external CollItem frame");
            events_pickups_begin(&e);
            while(item_status>0 && item.frame==rows) {
                if(input_status>0 && input.word[0]==(int64_t)rows) {
                    uint32_t start=(uint32_t)f.values[CM_TRACE_TICKS];
                    uint32_t span=(uint32_t)input.word[1]-start;
                    if(span>INT32_MAX || item.ticks-start>span)
                        return cm_fail(item_path,rows,"CollItem observation is outside the pre-MoveWorld phase");
                }
                if(apply_item_input(&item,item_path)) return 1;
                ++item_calls;
                item_status=next_item_input(item_file,&item,item_path);
                if(item_status<0) return 1;
            }
            if(events_pickups_end(&e,&w)) return 1;
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
        if(check_inventory) {
            int result;
            if(w.player_person>=w.person_count || w.persons[w.player_person].status==2 ||
               e.pc_dead_man || e.shop_requested.kind)
                return cm_fail(argv[2],rows,"death/shop frames require the full game-loop verifier");
            result=draw_state(&replay,rows,CM_DRAW_NORMAL);
            if(result<0) return 1;
            if(result>0) ++game_draws;
        }
        if(events_text_finish(&e,&w)) return 1;
        if(actor_path) {
            player_status=next_player_input(&actor_reader,&player,actor_path);
            if(player_status<0) return 1;
        }
        ++rows;
        status=replay_next(&replay,&f);
    }
    fclose(input_file);
    if(item_file) fclose(item_file);
    if(selector_file) fclose(selector_file);
    if(actor_reader.file) fclose(actor_reader.file);
    replay_close(&replay); snd_close(); world_close(&w);
    if(status<0) return 1;
    if(!moves || input_status>0) return cm_fail(argv[3],rows,"empty or unused MoveWorld observations");
    if(item_status>0) return cm_fail(item_path,rows,"unused external CollItem observations");
    if(player_status>0) return cm_fail(actor_path,rows,"unused PERSON0 HP comparisons");
    if(selector_status>0) return cm_fail(selector_path,rows,"unused selector input observations");
    printf("event reducer: %u frames %u independently computed state bytes matched; %u vehicle rows\n",rows,checks,vehicle_rows);
    if(check_inventory) {
        printf("inventory verifier: %u external CollItem calls; %u inventory/equipment bytes; %u selector bytes; %u PERSON0 HP comparisons\n",
               item_calls,inventory_checks,selector_checks,hp_checks);
        printf("unverified scope: %u Sim/guest-pointer bytes at131418; %u PC-owned global bytes (spellTimer/waveTimer/waveFrame)\n",
               sim_pointer_bytes,unverified_pc_globals);
        puts("selector scope: key count/index and shared map only; weapon/power count/index +144814/+14491C unverified");
        printf("selector observations: %u ProcessSelector pointer inputs; %u observed UI save/load resets\n",selector_calls,selector_resets);
        printf("draw phases: %u live normal-HUD draws; %u observed Save redraws; measured clocks only, no frame-tick fallback\n",
               game_draws,save_draws);
        puts("draw scope: live normal HUD only; death/shop/mid-trace Load require the full game loop; AI/RNG parity not claimed");
        puts("HP scope: PERSON0 changes must come from Events/CollItem; zero item calls prove stability, not pickup execution");
    }
    return 0;
}
