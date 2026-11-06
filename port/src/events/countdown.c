#include "events.h"
#include "ai.h"
#include "audio.h"

/* DrawConsole CM_HI.EXE01942F..019822; autoDxpnt at092630. */
int events_countdown(events_state *e, sim_state *s, ai_state *a, world *w)
{
    static const int32_t points[23]={24000,21600,19200,16800,14400,12000,
        9600,7200,4800,2400,1200,800,400,360,320,280,240,200,160,120,80,40,0};
    int32_t remaining,seconds,minutes;
    int sample=0;
    if(!e->autoDestruct) return 0;
    if(e->autoDestruct<1 || e->autoDestruct>=23)
        return cm_fail("DrawConsole/countdown",(uint32_t)e->autoDestruct,"invalid countdown phase");
    remaining=(int32_t)(e->autoDTime-e->ticks);
    if(remaining<=0) {
        e->autoDestruct=0;e->autoDMsgTimer=0;e->autoLFlag=0;
        e->autoMSlot=-1;e->pc_dead_man=1;
    } else if(remaining<points[e->autoDestruct]) {
        ++e->autoDestruct;e->autoDMsgTimer=e->ticks+160;
        e->autoLFlag=1;e->autoMSlot=-1;
    }
    if(!snd_enabled() && (int32_t)e->ticks>(int32_t)e->autoDMsgTimer) e->autoLFlag=0;
    if(!e->autoLFlag) {
        if(e->autoMSlot!=-1 && snd_done(e->autoMSlot,(unsigned)e->autoMSound)) e->autoMSlot=-1;
        if(e->autoMSlot==-1) e->autoMSound=0;
        return 0;
    }
    if(e->autoDestruct<1 || e->autoDestruct>23) return cm_fail("DrawConsole/countdown",0,"invalid announcement phase");
    seconds=points[e->autoDestruct-1]/40;minutes=seconds/60;
    if(e->autoMSlot!=-1 && snd_done(e->autoMSlot,(unsigned)e->autoMSound)) {
        e->autoMSlot=-1;++e->autoLFlag;
    }
    if(e->autoMSlot==-1) {
        e->autoMSound=0;
        if(minutes>0) {
            switch(e->autoLFlag) {
            case 1: sample=89;break;
            case 2: sample=90;break;
            case 3: sample=minutes+93;break;
            case 4: sample=minutes>1?92:91;break;
            case 5: e->autoLFlag=0;break;
            }
        } else if(seconds>9) {
            switch(e->autoLFlag) {
            case 1: sample=89;break;
            case 2: sample=seconds/10+102;break;
            case 3: sample=93;break;
            case 4: e->autoLFlag=0;break;
            }
        } else { sample=seconds+93;e->autoLFlag=0; }
    }
    if(minutes<=0 && seconds<2 && ai_launch_missile(a,s,w,w->player_object,0,52)) return -1;
    if(sample) {
        if(snd_enabled() && snd_sample_load((unsigned)sample)) return -1;
        e->autoMSound=sample;e->autoMSlot=snd_play((unsigned)sample,32767,32768);
    }
    return 0;
}
