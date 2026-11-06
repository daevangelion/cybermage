#include "ui.h"
#include "events.h"
#include "audio.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ARENA_ODDS_DRAWS 64
#define SLOT_REEL_SIZE 14u

static int bitmap(uint8_t *dst,int x,int y,const ui_bitmap *b)
{
    unsigned row,col;
    if(x<0 || y<0 || x>=320 || y>=240 || b->width>320-x || b->height>240-y)
        return cm_fail("gambling bitmap",0,"widget outside canvas");
    for(row=0;row<b->height;++row) for(col=0;col<b->width;++col) {
        uint8_t c=b->pixels[row*b->width+col];
        unsigned at=((unsigned)y+row)*2*CM_WIDTH+((unsigned)x+col)*2;
        gfx_store(dst+at,c);gfx_store(dst+at+1,c);
        gfx_store(dst+at+CM_WIDTH,c);gfx_store(dst+at+CM_WIDTH+1,c);
    }
    return 0;
}

static int text(const cm_ui *ui,uint8_t *dst,int center,int y,const char *s)
{
    int width;
    if(gfx_text_width(&ui->fonts[3],s,&width)) return -1;
    return ui_text_mode(dst,&ui->fonts[3],center-width/2,y,s,232,1,3,1);
}

static int amount(const cm_ui *ui,uint8_t *dst,int center,int y,const char *label,int32_t n)
{
    char s[64];
    int count=snprintf(s,sizeof s,"%s%d",label,n);
    if(count<0 || (unsigned)count>=sizeof s)
        return cm_fail("gambling label",0,"label exceeds display buffer");
    return text(ui,dst,center,y,s);
}

static int arena_pending(const events_state *e,const world *w)
{
    return e->PCBet>0 && e->PCBetPick>=0 && w->persons[57].status!=2 &&
        (w->persons[58].status!=2 || w->persons[59].status!=2 || w->persons[60].status!=2);
}

/* LoadArenaScreen4A398/LoadSlotMachine4BBE4: state belongs to Events. */
int ui_gambling_open(cm_ui *ui,events_state *e,world *w)
{
    unsigned i;
    ui->bet_amount=ui->bet_payout=0;ui->slot_result=0;ui->slot_message=0;
    ui->slot_spinning=0;ui->gambling_next=ui->ticks;
    if(ui->shop_kind==2) {
        if(w->scene!=2 || w->person_count<61 || e->event_count<243)
            return cm_fail("arena",0,"missing arena actors/events");
        if(e->PCBet<0 || e->PCBetPick < -1 || e->PCBetPick>1 ||
            e->PCBetOdds<2 || e->PCBetOdds>32767)
            return cm_fail("arena",0,"invalid saved bet");
        if(arena_pending(e,w)) { ui->shop_kind=0;return 0; }
        if(e->PCBet>0 && e->PCBetPick>=0) {
            int won=(e->PCBetPick==1)==(w->persons[57].status==2);
            if(won) {
                int64_t payout=(int64_t)e->PCBet*e->PCBetOdds;
                if(payout<0 || payout>INT32_MAX) return cm_fail("arena",0,"payout exceeds cash range");
                events_cash_add(e,(int32_t)payout);ui->bet_payout=(int32_t)payout;
            }
            ui->slot_result=won?1:-1;
            e->PCBetOdds=2;
            for(i=0;i<ARENA_ODDS_DRAWS;++i) {
                if(!world_rnd(&w->rng,2)) break;
                ++e->PCBetOdds;
            }
            if(i==ARENA_ODDS_DRAWS) return cm_fail("arena",0,"odds draw limit reached");
        }
        e->PCBet=0;e->PCBetPick=-1;
    } else {
        int32_t elapsed=(int32_t)(ui->ticks-(uint32_t)e->SlotVisitTime);
        unsigned minutes=elapsed>0?(unsigned)elapsed/2400u:0;
        for(i=0;i<minutes;++i) {
            int growth=world_rnd(&w->rng,15)+15;
            if(e->JackPot>INT32_MAX-growth) return cm_fail("slots",0,"jackpot exceeds cash range");
            e->JackPot+=growth;
        }
        for(i=0;i<3;++i) { ui->slot_reel[i]=(unsigned)world_rnd(&w->rng,14);ui->slot_steps[i]=0; }
    }
    snd_song_end();if(snd_song(6,1)) return -1;
    return 1;
}

/* CM_HI.EXE4C2D4..4C5EC, slotcomb96404. Profit plus stake is credited. */
static int slot_settle(cm_ui *ui,events_state *e,world *w)
{
    unsigned i,counts[4]={0};
    int multiplier=0;
    int64_t profit,credit;
    for(i=0;i<3;++i) {
        unsigned reel=ui->slot_reel[i];
        unsigned symbol=reel<SLOT_REEL_SIZE ? cm_u16(ui->slot_symbols+reel*2):4;
        if(symbol>3) return cm_fail("slotcomb",reel,"invalid reel symbol");
        ++counts[symbol];
    }
    ui->slot_message=0;
    if(counts[3]==2 && counts[2]==1) { multiplier=3;ui->slot_message=196; }
    else if(counts[3]==3) { multiplier=5;ui->slot_message=197; }
    else if(counts[2]==3) { multiplier=10;ui->slot_message=198; }
    else if(counts[0]==2 && counts[2]==1) { multiplier=20;ui->slot_message=199; }
    else if(counts[1]==3) { multiplier=world_rnd(&w->rng,30)+21;ui->slot_message=200; }
    else if(counts[0]==3) ui->slot_message=201;
    profit=ui->slot_message==201 ? e->JackPot:(int64_t)ui->bet_amount*multiplier;
    credit=profit+ui->bet_amount;
    if(profit<0 || profit>INT32_MAX || credit>INT32_MAX)
        return cm_fail("slots",0,"payout exceeds cash range");
    ui->slot_result=ui->slot_message?1:-1;ui->bet_payout=(int32_t)profit;
    if(ui->slot_result>0) {
        events_cash_add(e,(int32_t)credit);
        if(ui->slot_message==201) e->JackPot=1727;
    } else {
        int growth=ui->bet_amount*8/10;
        if(e->JackPot>INT32_MAX-growth) return cm_fail("slots",0,"jackpot exceeds cash range");
        e->JackPot+=growth;
    }
    ui->bet_amount=0;ui->slot_spinning=0;ui->gambling_next=ui->ticks+120;
    return 0;
}

static int clicked(const cm_input *in,int x,int y,const ui_bitmap *b)
{
    return x>=0 && y>=0 && x<320 && y<240 &&
        in->mouse_x>=x*2 && in->mouse_x<(x+b->width)*2 &&
        in->mouse_y>=y*2 && in->mouse_y<(y+b->height)*2;
}

int ui_gambling_step(cm_ui *ui,events_state *e,world *w,const cm_input *in)
{
    unsigned i,action=0;
    int arena=ui->shop_kind==2;
    if(ui->slot_spinning) {
        unsigned running=0;
        for(i=0;i<3;++i) if(ui->slot_steps[i]) {
            ui->slot_reel[i]=(ui->slot_reel[i]+1)%SLOT_REEL_SIZE;
            --ui->slot_steps[i];running|=ui->slot_steps[i];
        }
        if(!running && slot_settle(ui,e,w)) return -1;
        return 1;
    }
    if(in->pressed[1]) {
        if(arena && e->PCBet>0 && events_arena_start(e,w)) return -1;
        if(!arena) e->SlotVisitTime=(int32_t)ui->ticks;
        ui->shop_kind=0;snd_song_end();return 1;
    }
    if((int32_t)(ui->ticks-ui->gambling_next)<=0) return 1;
    for(i=0;i<10;++i) if(in->pressed[i?i+1:11]) action=i+4;
    if(in->pressed[14] || in->pressed[83]) action=2;
    if(in->pressed[28] || in->pressed[57]) action=1;
    if(arena && in->pressed[48]) action=14;
    if(arena && in->pressed[31]) action=15;
    if(in->mouse_pressed&1u) {
        unsigned bank=arena?1:3;
        unsigned first=arena?1:10,count=arena?12:14;
        for(i=0;i<count;++i) {
            unsigned at=first+i;
            int x=arena?ui->arena_x[at]:ui->slot_x[at];
            int y=arena?ui->arena_y[at]:ui->slot_y[at];
            if(clicked(in,x,y,&ui->shop_gfx[bank][at]))
                action=arena ? (i<2?i+2:(i+9)%10+4):i+1;
        }
        if(arena && clicked(in,37,159,&ui->shop_gfx[1][0])) action=14;
        if(arena && clicked(in,233,159,&ui->shop_gfx[1][0])) action=15;
    }
    if(!action) return 1;
    ui->gambling_next=ui->ticks+10;ui->slot_result=0;
    if(action>=4 && action<=13) {
        int limit=arena?9999999:999999;
        if(ui->bet_amount<limit) ui->bet_amount=ui->bet_amount*10+(int)(action-4);
    } else if(action==2) ui->bet_amount=0;
    else if(action==3) {
        if(arena && e->PCBet>0 && events_arena_start(e,w)) return -1;
        if(!arena)e->SlotVisitTime=(int32_t)ui->ticks;
        ui->shop_kind=0;snd_song_end();
    }
    else if(arena && (action==14 || action==15)) {
        int32_t bet=ui->bet_amount>e->cash?e->cash:ui->bet_amount;
        if(bet>0 && !e->PCBet) {
            e->PCBet=bet;e->PCBetPick=action==15;
            if(events_arena_start(e,w)) { e->PCBet=0;e->PCBetPick=-1;return -1; }
            events_cash_add(e,-bet);ui->shop_kind=0;snd_song_end();
        }
    } else if(action==1) {
        if(arena) {
            if(e->PCBet>0) {
                if(events_arena_start(e,w)) return -1;
                ui->shop_kind=0;snd_song_end();
            }
        } else if((int32_t)(ui->ticks-(uint32_t)e->SlotWaitTime)>0) {
            if(e->cash<=0) return 1;
            if(ui->bet_amount>e->cash) ui->bet_amount=e->cash;
            if(ui->bet_amount>0) {
                events_cash_add(e,-ui->bet_amount);ui->slot_spinning=1;
                for(i=0;i<3;++i) ui->slot_steps[i]=(unsigned)world_rnd(&w->rng,50)+50;
            }
        }
    }
    (void)snd_play(5,32767,32768);
    return 1;
}

int ui_gambling_draw(cm_ui *ui,const events_state *e,uint8_t *dst)
{
    unsigned x,y,i,bank=ui->shop_kind==2?1:3;
    for(y=0;y<CM_HEIGHT;++y) for(x=0;x<CM_WIDTH;++x)
        gfx_store(dst+y*CM_WIDTH+x,ui->shop_background[bank][y/2*320+x/2]);
    if(bank==3) {
        for(i=0;i<3;++i) {
            unsigned reel=ui->slot_reel[i],symbol=reel<SLOT_REEL_SIZE?cm_u16(ui->slot_symbols+reel*2):4;
            if(symbol>3) return cm_fail("slotcomb",reel,"invalid reel symbol");
            if(bitmap(dst,86+(int)i*55,117,&ui->shop_gfx[3][symbol+6])) return -1;
        }
        if(amount(ui,dst,320,172,"POT$",e->JackPot) ||
           amount(ui,dst,320,188,"BET$",ui->bet_amount) ||
           amount(ui,dst,90,304,"$",e->cash)) return -1;
        if(ui->slot_result>0) {
            if(text(ui,dst,320,304,ui->strings[204]) || amount(ui,dst,320,180,"$",ui->bet_payout)) return -1;
        } else if(ui->slot_result<0) { if(text(ui,dst,550,304,ui->strings[205])) return -1; }
        else if(text(ui,dst,550,304,ui->strings[208])) return -1;
    } else {
        if(e->PCBetPick>=0 && e->PCBetPick<=1 &&
           bitmap(dst,e->PCBetPick?233:37,159,&ui->shop_gfx[1][0])) return -1;
        if(text(ui,dst,320,224,ui->strings[192]) || text(ui,dst,320,248,ui->strings[193]) ||
           amount(ui,dst,320,264,"",e->PCBetOdds-1) || text(ui,dst,92,68,ui->strings[194]) ||
           amount(ui,dst,92,84,"$",ui->bet_amount) || text(ui,dst,548,68,ui->strings[195]) ||
           amount(ui,dst,548,84,"$",e->cash)) return -1;
        if(ui->slot_result && text(ui,dst,320,240,ui->strings[ui->slot_result>0?190:191])) return -1;
    }
    return 0;
}
