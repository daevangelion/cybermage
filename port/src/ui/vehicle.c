#include "ui.h"

#include <stdio.h>

/* CM_HI.EXE ConWinInfo95A68, TankWinInfo95AB8, CarWinInfo95AD8. */
int ui_game_view(const cm_ui *ui, int mode, int vehicle_type, ui_view *v)
{
    const int32_t *p;
    if (!ui->hand_tables_loaded || ui->con_size>4 ||
        (mode==7 && vehicle_type!=0 && vehicle_type!=1))
        return cm_fail("viewport",ui->con_size,"invalid gameplay view");
    p=ui->view_info[mode==7 ? (vehicle_type?7:5):ui->con_size];
    if (p[0]<=0 || p[1]<=0 || p[2]<0 || p[3]<0 ||
        p[0]>CM_WIDTH-p[2] || p[1]>CM_HEIGHT-p[3])
        return cm_fail("CM_HI.EXE",0x95A68,"view outside canvas");
    v->width=p[0]; v->height=p[1]; v->x=p[2]; v->y=p[3];
    return 0;
}

/* LoadBackScreen1FBF0: expand 320x240 art without covering live views. */
int ui_draw_backdrop(const cm_ui *ui, uint8_t *dst, int mode,
                       int vehicle_type, ui_view view)
{
    int x,y;
    unsigned bank=mode==7 ? (vehicle_type?2u:1u):0u;
    if (!ui->backscreen[bank] || view.x<0 || view.y<0 ||
        view.width<0 || view.height<0 || view.width>CM_WIDTH-view.x ||
        view.height>CM_HEIGHT-view.y)
        return cm_fail("backdrop",bank,"invalid view or backdrop");
    for (y=0;y<CM_HEIGHT;++y) for (x=0;x<CM_WIDTH;++x)
        if (x<view.x || y<view.y || x>=view.x+view.width || y>=view.y+view.height)
            gfx_store(dst+y*CM_WIDTH+x,ui->backscreen[bank][y/2*320+x/2]);
    return 0;
}

static int acar_bitmap(uint8_t *dst, int x, int y, const ui_bitmap *b)
{
    unsigned row,col;
    if (x<0 || x>=CM_WIDTH || y<0 || y>=CM_HEIGHT || !b->pixels ||
        b->width>(CM_WIDTH-x)/2 || b->height>(CM_HEIGHT-y)/2)
        return cm_fail("aircar bitmap",0,"bitmap outside canvas");
    for (row=0;row<b->height;++row) {
        int dy=y+(int)row*2;
        if (dy<0 || dy+1>=CM_HEIGHT) continue;
        for (col=0;col<b->width;++col) {
            int dx=x+(int)col*2;
            uint8_t color=b->pixels[row*b->width+col];
            if (dx>=0 && dx+1<CM_WIDTH && color) {
                gfx_store(dst+dy*CM_WIDTH+dx,color);
                gfx_store(dst+dy*CM_WIDTH+dx+1,color);
            }
        }
        /* ACarMonitor15AB5 copies the composed row, including transparent texels. */
        gfx_copy(dst+(dy+1)*CM_WIDTH+x,dst+dy*CM_WIDTH+x,b->width*2);
    }
    return 0;
}

static int vehicle_text(const cm_ui *ui, uint8_t *dst, int anchor,
                          int y, const char *s, int right)
{
    int width;
    if (gfx_text_width(&ui->fonts[3],s,&width)) return -1;
    return ui_text_mode(dst,&ui->fonts[3],anchor-(right?width:width/2),y,
                        s,174,1,0,1);
}

int ui_draw_vehicle(const cm_ui *ui, const world *w, uint8_t *dst,
                      int kind, int speed, int altitude)
{
    char text[32];
    int hp,max,percent,n;
    unsigned i;
    if (w->player_person>=w->person_count || (kind!=0 && kind!=1))
        return cm_fail("vehicle HUD",w->player_person,"invalid vehicle person/type");
    hp=(int16_t)((uint32_t)w->persons[w->player_person].unk_0038&65535);
    max=(int16_t)((uint32_t)w->persons[w->player_person].unk_0038>>16);
    if (max<=0 || hp<0 || hp>max)
        return cm_fail("vehicle HUD",w->player_person,"invalid vehicle health");
    percent=100*hp/max;
    if (!kind) {
        for (i=0;i<12;++i) {
            unsigned n=(unsigned)(percent/5*6);
            if (n>sizeof ui->TankBar) return cm_fail("vehicle HUD",n,"tank bar exceeds table");
            gfx_blit(dst,262,6+(int)i,ui->TankBar,n,1);
        }
        if (snprintf(text,sizeof text,"%3d%%",percent)<0) return -1;
        return ui_text_mode(dst,&ui->fonts[3],410,4,text,211,255,3,0);
    }
    for (i=0;i<5;++i)
        if (acar_bitmap(dst,ui->ACarX[i],ui->ACarY[i],&ui->acar[i])) return -1;
    if (vehicle_text(ui,dst,100,350,ui->strings[146],0)) return -1;
    if (snprintf(text,sizeof text,"%3d%%",percent)<0 ||
        vehicle_text(ui,dst,100,366,text,0)) return -1;
    n=snprintf(text,sizeof text," %3d %s",speed,ui->strings[147]);
    if (n<0 || (unsigned)n>=sizeof text)
        return cm_fail("STRINGS.MSG",147,"speed label exceeds display buffer");
    if (vehicle_text(ui,dst,576,350,text,1)) return -1;
    n=snprintf(text,sizeof text," %3d %s",altitude/655,ui->strings[148]);
    if (n<0 || (unsigned)n>=sizeof text)
        return cm_fail("STRINGS.MSG",148,"altitude label exceeds display buffer");
    if (vehicle_text(ui,dst,576,366,text,1)) return -1;
    return 0;
}
